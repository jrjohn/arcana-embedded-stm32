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
    """Create device table if not exists"""
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
    conn.commit()
    conn.close()

# ---------------------------------------------------------------------------
# Server secret for upload token signing
# ---------------------------------------------------------------------------

SERVER_SECRET = os.environ.get("SERVER_SECRET", "arcana-dev-secret-change-in-prod").encode()

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
def register():
    """TOFU device registration. Accepts FrameCodec frame with RegisterRequest protobuf."""
    data = request.get_data()

    # Decode FrameCodec frame
    result = frame_decode(data)
    if not result:
        return make_error_response("Invalid frame"), 400
    payload, flags, stream_id = result

    # Decode RegisterRequest protobuf
    fields = pb_decode_message(payload)
    device_id = fields.get(1, b"").decode() if isinstance(fields.get(1), bytes) else ""
    public_key = fields.get(2, b"")
    firmware_ver = fields.get(3, 0)

    if not device_id or len(public_key) != 64:
        return make_error_response("Missing device_id or invalid public_key"), 400

    public_key_hex = public_key.hex()

    print(f"[REG] device={device_id} pubkey={public_key_hex[:16]}... fw={firmware_ver:#06x}")

    conn = get_db()
    try:
        with conn.cursor() as cur:
            # Check if already registered
            cur.execute("SELECT id FROM user WHERE username=%s", (device_id,))
            existing = cur.fetchone()
            if existing:
                # Verify public key matches (TOFU security)
                cur.execute("SELECT public_key FROM device WHERE device_id=%s", (device_id,))
                dev = cur.fetchone()
                if dev and dev['public_key'] != public_key_hex:
                    resp_pb = encode_register_response(success=False, error="pubkey mismatch")
                    return frame_encode(resp_pb, stream_id=SID_REGISTER), 403, \
                        {"Content-Type": "application/octet-stream"}

                # Re-provision: new password, return full credentials
                import bcrypt
                mqtt_pass = secrets.token_hex(16)
                pass_hash = bcrypt.hashpw(mqtt_pass.encode(), bcrypt.gensalt(rounds=10)).decode()
                cur.execute("UPDATE user SET password_hash=%s WHERE username=%s",
                            (pass_hash, device_id))
                conn.commit()

                upload_token = generate_upload_token(device_id)
                topic_prefix = f"/arcana/{device_id}"
                print(f"[REG] RE-PROVISION: user={device_id} topic={topic_prefix}/#")

                resp_pb = encode_register_response(
                    success=True,
                    mqtt_user=device_id,
                    mqtt_pass=mqtt_pass,
                    mqtt_broker=MQTT_BROKER,
                    mqtt_port=MQTT_PORT,
                    upload_token=upload_token,
                    topic_prefix=topic_prefix,
                )
                return frame_encode(resp_pb, stream_id=SID_REGISTER), 200, \
                    {"Content-Type": "application/octet-stream"}

            # Generate MQTT password
            import bcrypt
            mqtt_pass = secrets.token_hex(16)  # 32 char random
            pass_hash = bcrypt.hashpw(mqtt_pass.encode(), bcrypt.gensalt(rounds=10)).decode()

            # Insert MQTT user
            cur.execute("INSERT INTO user (username, password_hash, is_admin) VALUES (%s, %s, 0)",
                        (device_id, pass_hash))
            user_id = cur.lastrowid

            # Insert ACL: /arcana/{deviceId}/# for sub(1), pub(2), subPattern(4)
            topic = f"/arcana/{device_id}/#"
            for rw in [1, 2, 4]:
                cur.execute("INSERT INTO acl (user_id, topic, rw) VALUES (%s, %s, %s)",
                            (user_id, topic, rw))

            # Store device public key
            cur.execute(
                "INSERT INTO device (device_id, public_key, firmware_ver) VALUES (%s, %s, %s)",
                (device_id, public_key_hex, firmware_ver))

            conn.commit()

        # Generate upload token
        upload_token = generate_upload_token(device_id)
        topic_prefix = f"/arcana/{device_id}"

        print(f"[REG] OK: user=dev-{device_id} topic={topic_prefix}/#")

        # Encode RegisterResponse protobuf → FrameCodec
        resp_pb = encode_register_response(
            success=True,
            mqtt_user=device_id,
            mqtt_pass=mqtt_pass,
            mqtt_broker=MQTT_BROKER,
            mqtt_port=MQTT_PORT,
            upload_token=upload_token,
            topic_prefix=topic_prefix,
        )
        return frame_encode(resp_pb, stream_id=SID_REGISTER), 200, \
            {"Content-Type": "application/octet-stream"}

    except Exception as e:
        conn.rollback()
        print(f"[REG] ERROR: {e}")
        resp_pb = encode_register_response(success=False, error=str(e)[:30])
        return frame_encode(resp_pb, stream_id=SID_REGISTER), 500, \
            {"Content-Type": "application/octet-stream"}
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
# File Upload (with Bearer token)
# ---------------------------------------------------------------------------

@app.route("/upload/<device_id>/<filename>", methods=["POST"])
def upload_file(device_id, filename):
    # Verify Bearer token
    auth = request.headers.get("Authorization", "")
    if auth.startswith("Bearer "):
        token = auth[7:]
        if not verify_upload_token(token, device_id):
            abort(401, "Invalid or expired upload token")
    # Allow without token for backward compatibility (TODO: enforce in production)

    if not filename.endswith(".ats"):
        abort(400, "Only .ats files accepted")

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

    app.run(host=args.host, port=args.port, debug=True, threaded=True, ssl_context=ssl_ctx)
