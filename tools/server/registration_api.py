#!/usr/bin/env python3
"""
Arcana Device Registration API

Endpoints:
  POST /api/register  — TOFU device registration (FrameCodec + protobuf)
  GET  /api/device/<id>/status — check registration status
  POST /upload/<device_id>/<filename> — file upload with Bearer token
  GET  /upload/<device_id>/<filename>/status — resume check

Requires: pip install flask pymysql bcrypt protobuf
"""

import os
import sys
import time
import hmac
import struct
import hashlib
import secrets
import json
import re
from pathlib import Path
from flask import Flask, request, jsonify, abort

# Add parent dir for shared modules
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# ---------------------------------------------------------------------------
# FrameCodec (same as STM32 / mqtt_crypto_test.py)
# ---------------------------------------------------------------------------

MAGIC = b"\xAC\xDA"
VERSION = 0x01
FLAG_FIN = 0x01

# Stream IDs for registration
SID_REGISTER = 0x10
SID_AUTH = 0x11

def crc16(data, init=0):
    crc = init
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return crc & 0xFFFF

def frame_encode(payload, flags=FLAG_FIN, stream_id=0):
    header = MAGIC + bytes([VERSION, flags, stream_id])
    header += struct.pack("<H", len(payload))
    body = header + payload
    return body + struct.pack("<H", crc16(body))

def frame_decode(data):
    if len(data) < 9:
        return None
    if data[0:2] != MAGIC or data[2] != VERSION:
        return None
    flags = data[3]
    stream_id = data[4]
    payload_len = struct.unpack("<H", data[5:7])[0]
    if len(data) != 7 + payload_len + 2:
        return None
    expected_crc = crc16(data[:7 + payload_len])
    received_crc = struct.unpack("<H", data[7 + payload_len:9 + payload_len])[0]
    if expected_crc != received_crc:
        return None
    return data[7:7 + payload_len], flags, stream_id

# ---------------------------------------------------------------------------
# ChaCha20 RFC 7539 — pure Python (matches STM32 ChaCha20.hpp, no deps)
# ---------------------------------------------------------------------------

def _chacha20_quarter_round(state, a, b, c, d):
    state[a] = (state[a] + state[b]) & 0xFFFFFFFF; state[d] ^= state[a]; state[d] = ((state[d] << 16) | (state[d] >> 16)) & 0xFFFFFFFF
    state[c] = (state[c] + state[d]) & 0xFFFFFFFF; state[b] ^= state[c]; state[b] = ((state[b] << 12) | (state[b] >> 20)) & 0xFFFFFFFF
    state[a] = (state[a] + state[b]) & 0xFFFFFFFF; state[d] ^= state[a]; state[d] = ((state[d] <<  8) | (state[d] >> 24)) & 0xFFFFFFFF
    state[c] = (state[c] + state[d]) & 0xFFFFFFFF; state[b] ^= state[c]; state[b] = ((state[b] <<  7) | (state[b] >> 25)) & 0xFFFFFFFF

def _chacha20_block(key_32: bytes, counter: int, nonce_12: bytes) -> bytes:
    """Generate one 64-byte ChaCha20 keystream block."""
    state = list(struct.unpack('<16I',
        b'expand 32-byte k' + key_32 + struct.pack('<I', counter) + nonce_12))
    working = state[:]
    for _ in range(10):  # 20 rounds = 10 double-rounds
        _chacha20_quarter_round(working, 0,4,8,12)
        _chacha20_quarter_round(working, 1,5,9,13)
        _chacha20_quarter_round(working, 2,6,10,14)
        _chacha20_quarter_round(working, 3,7,11,15)
        _chacha20_quarter_round(working, 0,5,10,15)
        _chacha20_quarter_round(working, 1,6,11,12)
        _chacha20_quarter_round(working, 2,7,8,13)
        _chacha20_quarter_round(working, 3,4,9,14)
    return struct.pack('<16I', *[(working[i] + state[i]) & 0xFFFFFFFF for i in range(16)])

def chacha20_encrypt(key_32: bytes, nonce_12: bytes, plaintext: bytes) -> bytes:
    """ChaCha20 encrypt/decrypt (symmetric, RFC 7539). Counter starts at 0."""
    result = bytearray()
    for i in range(0, len(plaintext), 64):
        block = _chacha20_block(key_32, i // 64, nonce_12)
        chunk = plaintext[i:i+64]
        result.extend(a ^ b for a, b in zip(chunk, block))
    return bytes(result)

def frame_encode_encrypted(payload: bytes, key_32: bytes,
                            flags=FLAG_FIN, stream_id=0) -> bytes:
    """FrameCodec frame with ChaCha20 encrypted payload: [nonce:12][ciphertext:N]"""
    nonce = os.urandom(12)
    ciphertext = chacha20_encrypt(key_32, nonce, payload)
    return frame_encode(nonce + ciphertext, flags, stream_id)

# ---------------------------------------------------------------------------
# Protobuf manual encode/decode (no .proto compile dependency on server)
# ---------------------------------------------------------------------------

def pb_encode_varint(value):
    result = b""
    while value > 0x7F:
        result += bytes([(value & 0x7F) | 0x80])
        value >>= 7
    result += bytes([value & 0x7F])
    return result

def pb_encode_field(field_number, wire_type, data):
    tag = pb_encode_varint((field_number << 3) | wire_type)
    if wire_type == 0:  # varint
        return tag + pb_encode_varint(data)
    elif wire_type == 2:  # length-delimited
        return tag + pb_encode_varint(len(data)) + data
    return tag

def pb_decode_message(data):
    """Generic protobuf decoder → dict of {field_number: value}"""
    fields = {}
    offset = 0
    while offset < len(data):
        tag_byte = data[offset]
        offset += 1
        field_number = tag_byte >> 3
        wire_type = tag_byte & 0x07

        if wire_type == 0:  # varint
            value = 0
            shift = 0
            while True:
                b = data[offset]
                offset += 1
                value |= (b & 0x7F) << shift
                shift += 7
                if not (b & 0x80):
                    break
            fields[field_number] = value
        elif wire_type == 2:  # length-delimited
            length = 0
            shift = 0
            while True:
                b = data[offset]
                offset += 1
                length |= (b & 0x7F) << shift
                shift += 7
                if not (b & 0x80):
                    break
            fields[field_number] = data[offset:offset + length]
            offset += length
        else:
            break  # skip unknown
    return fields

def encode_register_response(success, mqtt_user="", mqtt_pass="", mqtt_broker="",
                              mqtt_port=0, upload_token="", topic_prefix="", error=""):
    """Encode RegisterResponse protobuf"""
    payload = b""
    payload += pb_encode_field(1, 0, 1 if success else 0)  # success
    if mqtt_user:
        payload += pb_encode_field(2, 2, mqtt_user.encode())
    if mqtt_pass:
        payload += pb_encode_field(3, 2, mqtt_pass.encode())
    if mqtt_broker:
        payload += pb_encode_field(4, 2, mqtt_broker.encode())
    if mqtt_port:
        payload += pb_encode_field(5, 0, mqtt_port)
    if upload_token:
        payload += pb_encode_field(6, 2, upload_token.encode())
    if topic_prefix:
        payload += pb_encode_field(7, 2, topic_prefix.encode())
    if error:
        payload += pb_encode_field(8, 2, error.encode())
    return payload

# ---------------------------------------------------------------------------
# @arcana_protocol decorator — FrameCodec + protobuf + ChaCha20 auto
# ---------------------------------------------------------------------------
# Usage:
#   @arcana_protocol(
#       req_fields={1: ('device_id', 'str'), 2: ('public_key', 'bytes'), 3: ('firmware_ver', 'int')},
#       resp_fields={1: ('success', 'bool'), 2: ('mqtt_user', 'str'), ...},
#       encrypt_response=True
#   )
#   def handler(req):
#       return {"success": True, "mqtt_user": "..."}, 200

from functools import wraps

def arcana_protocol(req_fields, resp_fields, encrypt_response=False, sid=SID_REGISTER):
    """Decorator: auto decode/encode FrameCodec + protobuf, optional ChaCha20."""
    def decorator(f):
        @wraps(f)
        def wrapper(*args, **kwargs):
            # --- Decode request ---
            raw = request.get_data()
            result = frame_decode(raw)
            if not result:
                return _proto_error("Invalid frame", sid), 400, \
                    {"Content-Type": "application/octet-stream"}
            payload, flags, stream_id = result
            pb = pb_decode_message(payload)

            req = {}
            for field_num, (name, typ) in req_fields.items():
                val = pb.get(field_num)
                if val is None:
                    req[name] = None
                elif typ == 'str' and isinstance(val, bytes):
                    req[name] = val.decode()
                elif typ == 'int' and not isinstance(val, int):
                    req[name] = int(val)
                else:
                    req[name] = val
            # Keep raw bytes for encryption key and ECDH
            if 2 in pb and isinstance(pb[2], bytes):
                req['_public_key_raw'] = pb[2]
            if 4 in pb and isinstance(pb[4], bytes):
                req['_ecdh_pub_raw'] = pb[4]

            # --- Call handler ---
            resp_dict, status_code = f(req, *args, **kwargs)

            # --- Encode response ---
            resp_pb = b""
            for field_num, (name, typ) in resp_fields.items():
                val = resp_dict.get(name)
                if val is None or val == "" or val == 0 and typ != 'bool':
                    continue
                if typ == 'bool':
                    resp_pb += pb_encode_field(field_num, 0, 1 if val else 0)
                elif typ == 'int':
                    resp_pb += pb_encode_field(field_num, 0, int(val))
                elif typ == 'str':
                    resp_pb += pb_encode_field(field_num, 2,
                        val.encode() if isinstance(val, str) else val)
                elif typ == 'bytes':
                    resp_pb += pb_encode_field(field_num, 2, val)

            # --- Encrypt + frame ---
            if encrypt_response and req.get('_public_key_raw'):
                device_key = req['_public_key_raw'][:32]
                body = frame_encode_encrypted(resp_pb, device_key, stream_id=sid)
            else:
                body = frame_encode(resp_pb, stream_id=sid)

            return body, status_code, {"Content-Type": "application/octet-stream"}
        return wrapper
    return decorator

def _proto_error(msg, sid):
    resp_pb = encode_register_response(success=False, error=msg)
    return frame_encode(resp_pb, stream_id=sid)

# ---------------------------------------------------------------------------
# MySQL
# ---------------------------------------------------------------------------

import pymysql

DB_CONFIG = {
    "host": os.environ.get("MYSQL_HOST", "127.0.0.1"),
    "port": int(os.environ.get("MYSQL_PORT", "3306")),
    "user": os.environ.get("MYSQL_USER", "root"),
    "password": os.environ.get("MYSQL_PASSWORD", "5YzYziFT3g"),
    "database": os.environ.get("MYSQL_DB", "mqtt"),
    "charset": "utf8mb4",
}

def get_db():
    return pymysql.connect(**DB_CONFIG, cursorclass=pymysql.cursors.DictCursor)

def init_db():
    """Create device + device_token tables if not exist"""
    conn = get_db()
    with conn.cursor() as cur:
        cur.execute("""
            CREATE TABLE IF NOT EXISTS device (
                id INT AUTO_INCREMENT PRIMARY KEY,
                device_id VARCHAR(16) UNIQUE NOT NULL,
                public_key VARCHAR(130) NOT NULL,
                firmware_ver INT DEFAULT 0,
                registered_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
            )
        """)
        cur.execute("""
            CREATE TABLE IF NOT EXISTS device_token (
                id            INT(11) AUTO_INCREMENT PRIMARY KEY,
                client_id     VARCHAR(48)   NOT NULL,
                token_type    VARCHAR(40)   DEFAULT 'ecdh_p256',
                device_pub    VARCHAR(130)  NOT NULL,
                scope         TEXT          DEFAULT NULL,
                revoked       TINYINT(1)   DEFAULT 0,
                issued_at     INT(11)      NOT NULL,
                expires_in    INT(11)      NOT NULL,
                user_id       INT(11)      DEFAULT NULL,
                firmware_ver  VARCHAR(40)   DEFAULT NULL,
                count         INT(11)      NOT NULL DEFAULT 1,
                remote_ip     VARCHAR(45)   DEFAULT NULL,
                updatedate    TIMESTAMP    DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
            )
        """)
    conn.commit()
    conn.close()

# ---------------------------------------------------------------------------
# ECDH + ECDSA for registration key exchange
# ---------------------------------------------------------------------------

from cryptography.hazmat.primitives.asymmetric import ec, utils as ec_utils
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

# Company private key — sole root secret (from env var)
_company_priv_hex = os.environ.get("COMPANY_PRIV_HEX", "")
if not _company_priv_hex:
    _env_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".env")
    if os.path.exists(_env_file):
        for line in open(_env_file):
            if line.startswith("COMPANY_PRIV_HEX="):
                _company_priv_hex = line.strip().split("=", 1)[1].strip()

if _company_priv_hex:
    COMPANY_PRIV = ec.derive_private_key(
        int.from_bytes(bytes.fromhex(_company_priv_hex), 'big'), ec.SECP256R1())
    COMPANY_PUB = COMPANY_PRIV.public_key()
    print(f"  Company key: loaded ({_company_priv_hex[:8]}...)")
else:
    COMPANY_PRIV = None
    COMPANY_PUB = None
    print("  Company key: NOT SET (ECDH/ECDSA disabled)")


def derive_server_keypair(device_id: str, count: int):
    """Derive deterministic EC P-256 keypair from COMPANY_PRIV + device_id + count."""
    if not COMPANY_PRIV:
        return None, None
    company_priv_bytes = COMPANY_PRIV.private_numbers().private_value.to_bytes(32, 'big')
    salt = f"{device_id}:{count}".encode()
    seed = HKDF(
        algorithm=hashes.SHA256(), length=32,
        salt=salt, info=b"ARCANA-SERVER-KEY"
    ).derive(company_priv_bytes)
    # Clamp seed to valid P-256 range
    seed_int = int.from_bytes(seed, 'big')
    # P-256 order
    n = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
    seed_int = (seed_int % (n - 1)) + 1
    priv = ec.derive_private_key(seed_int, ec.SECP256R1())
    return priv, priv.public_key()


def ecdh_derive_comm_key(server_priv, device_pub_bytes: bytes, device_id: str) -> bytes:
    """ECDH shared secret → HKDF → 32-byte comm_key."""
    pub_nums = ec.EllipticCurvePublicNumbers(
        x=int.from_bytes(device_pub_bytes[:32], 'big'),
        y=int.from_bytes(device_pub_bytes[32:64], 'big'),
        curve=ec.SECP256R1()
    )
    device_pub = pub_nums.public_key()
    shared = server_priv.exchange(ec.ECDH(), device_pub)
    # HKDF: same as device side — PRK=HMAC(salt=device_id, ikm=shared), expand with "ARCANA-COMM"
    prk = hmac.new(device_id.encode()[:8], shared, hashlib.sha256).digest()
    info_block = b"ARCANA-COMM\x01"
    comm_key = hmac.new(prk, info_block, hashlib.sha256).digest()
    return comm_key


def ecdsa_sign_server_pub(server_pub_bytes: bytes, device_id: str) -> bytes:
    """Sign SHA256(server_pub + device_id) with company private key. Returns DER."""
    if not COMPANY_PRIV:
        return b""
    data = server_pub_bytes + device_id.encode()[:8]
    sig = COMPANY_PRIV.sign(data, ec.ECDSA(hashes.SHA256()))
    return sig  # DER format


def ec_pub_to_raw(pub_key) -> bytes:
    """Export EC public key as raw x||y (64 bytes)."""
    nums = pub_key.public_numbers()
    return nums.x.to_bytes(32, 'big') + nums.y.to_bytes(32, 'big')


# ---------------------------------------------------------------------------
# Server secret for upload token signing
# ---------------------------------------------------------------------------

_DEFAULT_SECRET = "arcana-dev-secret-change-in-prod"
_secret_str = os.environ.get("SERVER_SECRET", _DEFAULT_SECRET)
if _secret_str == _DEFAULT_SECRET and os.environ.get("FLASK_ENV") == "production":
    raise RuntimeError("SERVER_SECRET must be set in production — refusing to start with default")
SERVER_SECRET = _secret_str.encode()

def generate_upload_token(device_id, expiry_hours=24):
    expiry = int(time.time()) + expiry_hours * 3600
    token_data = f"{device_id}|{expiry}"
    sig = hmac.new(SERVER_SECRET, token_data.encode(), hashlib.sha256).hexdigest()[:32]
    return f"{token_data}|{sig}"

def verify_upload_token(token, device_id):
    try:
        parts = token.split("|")
        if len(parts) != 3:
            return False
        tok_device, tok_expiry, tok_sig = parts
        if tok_device != device_id:
            return False
        if int(tok_expiry) < time.time():
            return False
        expected = hmac.new(SERVER_SECRET, f"{tok_device}|{tok_expiry}".encode(),
                           hashlib.sha256).hexdigest()[:32]
        return hmac.compare_digest(expected, tok_sig)
    except Exception:
        return False

# ---------------------------------------------------------------------------
# Flask App
# ---------------------------------------------------------------------------

app = Flask(__name__)
UPLOAD_DIR = os.environ.get("UPLOAD_DIR", "./uploads")
MQTT_BROKER = os.environ.get("MQTT_BROKER", "arcana.boo")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "8883"))  # MQTTS TLS port

@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok", "time": int(time.time())})

@app.route("/api/register", methods=["POST"])
@arcana_protocol(
    req_fields={1: ('device_id', 'str'), 2: ('public_key', 'bytes'), 3: ('firmware_ver', 'int'),
                4: ('ecdh_pub', 'bytes')},
    resp_fields={
        1: ('success', 'bool'), 2: ('mqtt_user', 'str'), 3: ('mqtt_pass', 'str'),
        4: ('mqtt_broker', 'str'), 5: ('mqtt_port', 'int'), 6: ('upload_token', 'str'),
        7: ('topic_prefix', 'str'), 8: ('error', 'str'),
        9: ('server_pub', 'bytes'), 10: ('ecdsa_sig', 'bytes'),
    },
    encrypt_response=True,
)
def register(req):
    """TOFU device registration — handler receives/returns plain dicts."""
    device_id = req.get('device_id', '')
    public_key = req.get('public_key', b'')
    firmware_ver = req.get('firmware_ver', 0) or 0

    if not device_id or not isinstance(public_key, bytes) or len(public_key) != 64:
        return {"success": False, "error": "bad request"}, 400

    public_key_hex = public_key.hex()
    print(f"[REG] device={device_id} pubkey={public_key_hex[:16]}... fw={firmware_ver:#06x}")

    import bcrypt
    conn = get_db()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT id FROM user WHERE username=%s", (device_id,))
            existing = cur.fetchone()

            if existing:
                # Verify public key matches (TOFU)
                cur.execute("SELECT public_key FROM device WHERE device_id=%s", (device_id,))
                dev = cur.fetchone()
                if dev and dev['public_key'] != public_key_hex:
                    return {"success": False, "error": "pubkey mismatch"}, 403

                # Re-provision
                mqtt_pass = secrets.token_hex(16)
                pass_hash = bcrypt.hashpw(mqtt_pass.encode(), bcrypt.gensalt(rounds=10)).decode()
                cur.execute("UPDATE user SET password_hash=%s WHERE username=%s",
                            (pass_hash, device_id))
                conn.commit()
                print(f"[REG] RE-PROVISION: {device_id}")
            else:
                # First registration
                mqtt_pass = secrets.token_hex(16)
                pass_hash = bcrypt.hashpw(mqtt_pass.encode(), bcrypt.gensalt(rounds=10)).decode()
                cur.execute("INSERT INTO user (username, password_hash, is_admin) VALUES (%s, %s, 0)",
                            (device_id, pass_hash))
                user_id = cur.lastrowid
                topic = f"/arcana/{device_id}/#"
                for rw in [1, 2, 4]:
                    cur.execute("INSERT INTO acl (user_id, topic, rw) VALUES (%s, %s, %s)",
                                (user_id, topic, rw))
                cur.execute("INSERT INTO device (device_id, public_key, firmware_ver) VALUES (%s, %s, %s)",
                            (device_id, public_key_hex, firmware_ver))
                conn.commit()
                print(f"[REG] NEW: {device_id}")

        # --- ECDH key exchange ---
        ecdh_pub_raw = req.get('_ecdh_pub_raw', b'')
        server_pub_bytes = b""
        ecdsa_sig = b""

        if COMPANY_PRIV:
            with conn.cursor() as cur2:
                cur2.execute("SELECT COALESCE(MAX(count),0)+1 AS next_count FROM device_token WHERE client_id=%s",
                             (device_id,))
                reg_count = cur2.fetchone()['next_count']

            if len(ecdh_pub_raw) == 64:
                # Device sent ECDH pub → full ECDH key exchange
                srv_priv, srv_pub = derive_server_keypair(device_id, reg_count)
                if srv_priv:
                    server_pub_bytes = ec_pub_to_raw(srv_pub)
                    comm_key = ecdh_derive_comm_key(srv_priv, ecdh_pub_raw, device_id)
                    ecdsa_sig = ecdsa_sign_server_pub(server_pub_bytes, device_id)
                    print(f"[REG] ECDH comm_key={comm_key[:8].hex()}... count={reg_count}")
            else:
                # No ECDH pub → server-side only (legacy)
                company_priv_bytes = COMPANY_PRIV.private_numbers().private_value.to_bytes(32, 'big')
                salt = f"{device_id}:{reg_count}".encode()
                comm_key = HKDF(
                    algorithm=hashes.SHA256(), length=32,
                    salt=salt, info=b"ARCANA-COMM"
                ).derive(company_priv_bytes)
                print(f"[REG] HKDF comm_key={comm_key[:8].hex()}... count={reg_count}")

            # Get user_id
            with conn.cursor() as cur2:
                cur2.execute("SELECT id FROM user WHERE username=%s", (device_id,))
                uid_row = cur2.fetchone()
                uid = uid_row['id'] if uid_row else None

            # Revoke old + insert new token
            token_type = 'ecdh_p256' if len(ecdh_pub_raw) == 64 else 'hkdf_sha256'
            with conn.cursor() as cur2:
                cur2.execute("UPDATE device_token SET revoked=1 WHERE client_id=%s AND revoked=0",
                             (device_id,))
                cur2.execute("""INSERT INTO device_token
                    (client_id, token_type, device_pub, scope,
                     issued_at, expires_in, user_id, firmware_ver, count, remote_ip)
                    VALUES (%s, %s, %s, 'mqtt:sensor mqtt:command',
                            %s, 31536000, %s, %s, %s, %s)""",
                    (device_id, token_type,
                     ecdh_pub_raw.hex() if ecdh_pub_raw else '',
                     int(time.time()), uid, str(firmware_ver),
                     reg_count, request.remote_addr))
            conn.commit()
            print(f"[REG] device_token: {device_id} type={token_type} count={reg_count}")

        resp = {
            "success": True,
            "mqtt_user": device_id,
            "mqtt_pass": mqtt_pass,
            "mqtt_broker": MQTT_BROKER,
            "mqtt_port": MQTT_PORT,
            "upload_token": generate_upload_token(device_id),
            "topic_prefix": f"/arcana/{device_id}",
        }
        if server_pub_bytes:
            resp["server_pub"] = server_pub_bytes  # 64-byte EC pub for ECDH
        if ecdsa_sig:
            resp["ecdsa_sig"] = ecdsa_sig

        return resp, 200

    except Exception as e:
        conn.rollback()
        print(f"[REG] ERROR: {e}")
        return {"success": False, "error": str(e)[:30]}, 500
    finally:
        conn.close()

@app.route("/api/device/<device_id>/status", methods=["GET"])
def device_status(device_id):
    conn = get_db()
    with conn.cursor() as cur:
        cur.execute("SELECT device_id, firmware_ver, registered_at FROM device WHERE device_id=%s",
                    (device_id,))
        dev = cur.fetchone()
    conn.close()
    if not dev:
        return jsonify({"registered": False}), 404
    return jsonify({
        "registered": True,
        "device_id": dev["device_id"],
        "firmware_ver": dev["firmware_ver"],
        "registered_at": str(dev["registered_at"]),
    })

# ---------------------------------------------------------------------------
# Device Key Query API (for MQTT/HTTP clients)
# ---------------------------------------------------------------------------

@app.route("/api/device/<device_id>/key", methods=["GET"])
def get_device_key(device_id):
    """Query comm_key for a device. Derived on-the-fly, never stored."""
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        abort(401, "Missing Authorization: Bearer <token>")
    # Accept any valid upload token for now (TODO: dedicated admin token)
    if not verify_upload_token(auth[7:], device_id):
        # Also accept server secret as admin token
        if auth[7:] != _secret_str:
            abort(401, "Invalid token")

    if not COMPANY_PRIV:
        abort(503, "ECDH not configured (COMPANY_PRIV_HEX not set)")

    conn = get_db()
    with conn.cursor() as cur:
        cur.execute("""
            SELECT device_pub, count, issued_at, expires_in
            FROM device_token
            WHERE client_id=%s AND revoked=0
            ORDER BY issued_at DESC LIMIT 1
        """, (device_id,))
        row = cur.fetchone()
    conn.close()

    if not row:
        abort(404, "No active token for device")

    # Derive comm_key on-the-fly
    if row.get("device_pub") and len(row["device_pub"]) == 128:
        # ECDH mode: derive server_priv, compute shared secret with device_pub
        device_pub_bytes = bytes.fromhex(row["device_pub"])
        srv_priv, _ = derive_server_keypair(device_id, row["count"])
        if not srv_priv:
            abort(500, "Key derivation failed")
        comm_key = ecdh_derive_comm_key(srv_priv, device_pub_bytes, device_id)
    else:
        # HKDF-only mode (legacy)
        company_priv_bytes = COMPANY_PRIV.private_numbers().private_value.to_bytes(32, 'big')
        salt = f"{device_id}:{row['count']}".encode()
        comm_key = HKDF(
            algorithm=hashes.SHA256(), length=32,
            salt=salt, info=b"ARCANA-COMM"
        ).derive(company_priv_bytes)

    return jsonify({
        "device_id": device_id,
        "comm_key": comm_key.hex(),
        "issued_at": row["issued_at"],
        "expires_in": row["expires_in"],
        "count": row["count"],
    })

# ---------------------------------------------------------------------------
# File Upload (with Bearer token)
# ---------------------------------------------------------------------------

@app.route("/upload/<device_id>/<filename>", methods=["POST"])
def upload_file(device_id, filename):
    # Validate path parameters (prevent traversal)
    import re
    if not re.fullmatch(r'[0-9A-Fa-f]{4,16}', device_id):
        abort(400, "Invalid device_id")
    if not re.fullmatch(r'[\w\-]+\.ats', filename):
        abort(400, "Invalid filename")

    # Verify Bearer token (mandatory)
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        abort(401, "Missing Authorization: Bearer <token>")
    token = auth[7:]
    if not verify_upload_token(token, device_id):
        abort(401, "Invalid or expired upload token")

    device_dir = Path(UPLOAD_DIR) / device_id
    device_dir.mkdir(parents=True, exist_ok=True)
    filepath = device_dir / filename

    # Content-Range support
    content_range = request.headers.get("Content-Range")
    write_offset = 0
    if content_range:
        try:
            range_spec = content_range.replace("bytes ", "")
            range_part, total_part = range_spec.split("/")
            start, end = range_part.split("-")
            write_offset = int(start)
        except (ValueError, IndexError):
            abort(400, f"Invalid Content-Range: {content_range}")

    mode = "r+b" if (write_offset > 0 and filepath.exists()) else "wb"
    written = 0
    with open(filepath, mode) as f:
        if write_offset > 0:
            f.seek(write_offset)
        while True:
            chunk = request.stream.read(65536)
            if not chunk:
                break
            f.write(chunk)
            written += len(chunk)

    file_size = filepath.stat().st_size
    print(f"[UPL] {device_id}/{filename}: {written}B @ offset {write_offset} → {file_size}B")
    return jsonify({"status": "ok", "written": written, "file_size": file_size})

@app.route("/upload/<device_id>/<filename>/status", methods=["GET"])
def upload_status(device_id, filename):
    filepath = Path(UPLOAD_DIR) / device_id / filename
    if not filepath.exists():
        return jsonify({"exists": False, "size": 0})
    return jsonify({"exists": True, "size": filepath.stat().st_size})

@app.route("/uploads/<device_id>", methods=["GET"])
def list_files(device_id):
    device_dir = Path(UPLOAD_DIR) / device_id
    if not device_dir.exists():
        return jsonify({"device_id": device_id, "files": [], "count": 0})
    files = []
    for f in sorted(device_dir.iterdir()):
        if f.is_file():
            files.append({"name": f.name, "size": f.stat().st_size})
    return jsonify({"device_id": device_id, "files": files, "count": len(files)})

# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def make_error_response(msg):
    resp_pb = encode_register_response(success=False, error=msg)
    return frame_encode(resp_pb, stream_id=SID_REGISTER), {"Content-Type": "application/octet-stream"}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Arcana Registration + Upload API")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--upload-dir", default="./uploads")
    args = parser.parse_args()

    UPLOAD_DIR = args.upload_dir
    os.makedirs(UPLOAD_DIR, exist_ok=True)

    init_db()
    print(f"Arcana Registration API")
    print(f"  MySQL: {DB_CONFIG['host']}:{DB_CONFIG['port']}/{DB_CONFIG['database']}")
    print(f"  MQTT:  {MQTT_BROKER}:{MQTT_PORT}")
    print(f"  Upload: {os.path.abspath(UPLOAD_DIR)}")
    print(f"  Listen: http://{args.host}:{args.port}")
    print()

    app.config['MAX_CONTENT_LENGTH'] = None

    # TLS: use Let's Encrypt certs if available
    ssl_ctx = None
    cert = os.environ.get("TLS_CERT", "/certs/fullchain.pem")
    key = os.environ.get("TLS_KEY", "/certs/privkey.pem")
    if os.path.exists(cert) and os.path.exists(key):
        import ssl
        ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_ctx.load_cert_chain(cert, key)
        print(f"  TLS: {cert}")
    else:
        print(f"  TLS: disabled (no certs)")

    is_prod = os.environ.get("FLASK_ENV") == "production"
    app.run(host=args.host, port=args.port, debug=not is_prod, threaded=True, ssl_context=ssl_ctx)
