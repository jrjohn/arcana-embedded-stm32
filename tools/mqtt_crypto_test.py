#!/usr/bin/env python3
"""
MQTT Crypto Test — send encrypted protobuf commands to STM32, decode responses.

Implements the same protocol as ESP32/STM32 CryptoEngine:
  Frame:   [magic:2][ver:1][flags:1][sid:1][len:2 LE][payload:N][crc:2 LE]
  Crypto:  [counter:4 LE][ciphertext:N][tag:8]  (AES-256-CCM)
  Payload: protobuf CmdRequest / CmdResponse

Usage:
  python3 tools/mqtt_crypto_test.py                    # Ping (default)
  python3 tools/mqtt_crypto_test.py --cmd ping
  python3 tools/mqtt_crypto_test.py --cmd fw_version
  python3 tools/mqtt_crypto_test.py --cmd temperature
  python3 tools/mqtt_crypto_test.py --no-encrypt       # plaintext mode
"""

import argparse
import hashlib
import struct
import sys
import time

# --- Dependencies ---
try:
    from cryptography.hazmat.primitives.ciphers.aead import AESCCM
except ImportError:
    sys.exit("pip install cryptography")

try:
    import paho.mqtt.client as mqtt
except ImportError:
    sys.exit("pip install paho-mqtt")

try:
    from google.protobuf import descriptor_pool
    # Use compiled protobuf if available
    HAS_PROTOBUF = True
except ImportError:
    HAS_PROTOBUF = False

# ---------------------------------------------------------------------------
# Protobuf (manual encode/decode — no .proto compile dependency)
# ---------------------------------------------------------------------------

def pb_encode_varint(value):
    """Encode unsigned varint."""
    out = b""
    while value > 0x7F:
        out += bytes([(value & 0x7F) | 0x80])
        value >>= 7
    out += bytes([value & 0x7F])
    return out

def pb_encode_field(field_number, wire_type, data):
    """Encode a protobuf field."""
    tag = (field_number << 3) | wire_type
    return pb_encode_varint(tag) + data

def pb_encode_uint32(field_number, value):
    """Encode uint32 field (wire type 0 = varint)."""
    return pb_encode_field(field_number, 0, pb_encode_varint(value))

def pb_encode_bytes(field_number, data):
    """Encode bytes field (wire type 2 = length-delimited)."""
    return pb_encode_field(field_number, 2,
                           pb_encode_varint(len(data)) + data)

def encode_cmd_request(cluster, command, payload=b""):
    """Encode CmdRequest protobuf."""
    msg = b""
    if cluster != 0:
        msg += pb_encode_uint32(1, cluster)
    if command != 0:
        msg += pb_encode_uint32(2, command)
    if payload:
        msg += pb_encode_bytes(3, payload)
    return msg

def pb_decode_varint(data, offset):
    """Decode unsigned varint, return (value, new_offset)."""
    result = 0
    shift = 0
    while offset < len(data):
        b = data[offset]
        offset += 1
        result |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            return result, offset
        shift += 7
    raise ValueError("Truncated varint")

def decode_cmd_response(data):
    """Decode CmdResponse protobuf. Returns dict."""
    result = {"cluster": 0, "command": 0, "status": 0, "payload": b""}
    offset = 0
    while offset < len(data):
        tag, offset = pb_decode_varint(data, offset)
        field_number = tag >> 3
        wire_type = tag & 0x07

        if wire_type == 0:  # varint
            value, offset = pb_decode_varint(data, offset)
            if field_number == 1: result["cluster"] = value
            elif field_number == 2: result["command"] = value
            elif field_number == 3: result["status"] = value
        elif wire_type == 2:  # length-delimited
            length, offset = pb_decode_varint(data, offset)
            value = data[offset:offset + length]
            offset += length
            if field_number == 4: result["payload"] = value
        else:
            raise ValueError(f"Unsupported wire type {wire_type}")
    return result

# ---------------------------------------------------------------------------
# CRC-16 (polynomial 0x8408, same as FrameCodec / esp_crc16_le)
# ---------------------------------------------------------------------------

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

# ---------------------------------------------------------------------------
# FrameCodec
# ---------------------------------------------------------------------------

MAGIC = b"\xAC\xDA"
VERSION = 0x01
FLAG_FIN = 0x01

def frame_encode(payload, flags=FLAG_FIN, stream_id=0):
    """Wrap payload in frame: [magic:2][ver:1][flags:1][sid:1][len:2 LE][payload][crc:2 LE]"""
    header = MAGIC + bytes([VERSION, flags, stream_id])
    header += struct.pack("<H", len(payload))
    body = header + payload
    crc = crc16(body)
    return body + struct.pack("<H", crc)

def frame_decode(data):
    """Extract payload from frame. Returns (payload, flags, stream_id) or None."""
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
# CryptoEngine (AES-256-CCM, compatible with STM32/ESP32)
# ---------------------------------------------------------------------------

class CryptoEngine:
    KEY_LEN = 32
    TAG_LEN = 8
    COUNTER_LEN = 4
    OVERHEAD = COUNTER_LEN + TAG_LEN  # 12
    NONCE_PREFIX_LEN = 9
    NONCE_LEN = NONCE_PREFIX_LEN + COUNTER_LEN  # 13

    def __init__(self, key_hex):
        key = bytes.fromhex(key_hex)
        assert len(key) == self.KEY_LEN

        # Derive nonce prefix: SHA256(key || "ARCANA")[0..8]
        h = hashlib.sha256(key + b"ARCANA").digest()
        self.nonce_prefix = h[:self.NONCE_PREFIX_LEN]

        self.aesccm = AESCCM(key, tag_length=self.TAG_LEN)
        self.tx_counter = 0
        self.rx_counter = -1  # no baseline yet

    def _build_nonce(self, counter):
        return self.nonce_prefix + struct.pack("<I", counter)

    def encrypt(self, plaintext):
        """Encrypt: returns [counter:4 LE][ciphertext+tag]"""
        counter = self.tx_counter
        self.tx_counter += 1
        nonce = self._build_nonce(counter)
        ct = self.aesccm.encrypt(nonce, plaintext, None)
        # ct = ciphertext + tag (cryptography lib appends tag)
        # We need: [counter][ciphertext][tag] where tag is last 8 bytes
        ciphertext = ct[:-self.TAG_LEN]
        tag = ct[-self.TAG_LEN:]
        return struct.pack("<I", counter) + ciphertext + tag

    def decrypt(self, data):
        """Decrypt: [counter:4 LE][ciphertext][tag:8] -> plaintext"""
        if len(data) < self.OVERHEAD:
            raise ValueError("Too short")
        counter = struct.unpack("<I", data[:4])[0]

        # Replay protection
        if counter <= self.rx_counter:
            raise ValueError(f"Replay: {counter} <= {self.rx_counter}")

        nonce = self._build_nonce(counter)
        # cryptography lib expects ct+tag concatenated
        ct_and_tag = data[self.COUNTER_LEN:]
        plaintext = self.aesccm.decrypt(nonce, ct_and_tag, None)

        self.rx_counter = counter
        return plaintext

# ---------------------------------------------------------------------------
# Command definitions
# ---------------------------------------------------------------------------

CLUSTER_SYSTEM = 0x00
CLUSTER_SENSOR = 0x01
CLUSTER_DEVICE = 0x02

COMMANDS = {
    "ping":        (CLUSTER_SYSTEM, 0x01),
    "fw_version":  (CLUSTER_SYSTEM, 0x02),
    "compile_time":(CLUSTER_SYSTEM, 0x03),
    "temperature": (CLUSTER_SENSOR, 0x02),
    "accel":       (CLUSTER_SENSOR, 0x03),
    "light":       (CLUSTER_SENSOR, 0x04),
    "model":       (CLUSTER_DEVICE, 0x01),
    "serial":      (CLUSTER_DEVICE, 0x02),
}

STATUS_NAMES = {0: "OK", 1: "NotFound", 2: "InvalidParam", 3: "Busy", 4: "Error"}

# ---------------------------------------------------------------------------
# MQTT client
# ---------------------------------------------------------------------------

PSK_HEX = "dfcb40b40d06ac9132b23f15c160a1cdaa21ad7637e36dddd8e908d34a656bb3"
BROKER = "iot.somnics.cloud"
PORT = 443
TOPIC_CMD = "/arcana/cmd"
TOPIC_RSP = "/arcana/rsp"

def main():
    parser = argparse.ArgumentParser(description="MQTT Crypto Command Test")
    parser.add_argument("--cmd", default="ping", choices=COMMANDS.keys(),
                        help="Command to send (default: ping)")
    parser.add_argument("--no-encrypt", action="store_true",
                        help="Send plaintext (no AES-256-CCM)")
    parser.add_argument("--binary", action="store_true",
                        help="Use STM32 binary codec instead of protobuf")
    parser.add_argument("--broker", default=BROKER)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--timeout", type=int, default=5, help="Response timeout (sec)")
    args = parser.parse_args()

    cluster, command = COMMANDS[args.cmd]
    crypto = None if args.no_encrypt else CryptoEngine(PSK_HEX)

    if args.binary:
        # STM32 binary codec: [cluster][commandId][paramsLen][params...]
        payload = bytes([cluster, command, 0])  # no params
        print(f"[TX] {args.cmd}: binary codec {payload.hex()}")
    else:
        # Protobuf
        payload = encode_cmd_request(cluster, command)
        print(f"[TX] {args.cmd}: cluster=0x{cluster:02X} cmd=0x{command:02X} pb={len(payload)}B")

    # Encrypt
    if crypto:
        encrypted = crypto.encrypt(payload)
        print(f"[TX] Encrypted: {len(encrypted)}B (counter={crypto.tx_counter - 1})")
    else:
        encrypted = payload
        print(f"[TX] Plaintext: {len(encrypted)}B")

    # Frame
    framed = frame_encode(encrypted)
    print(f"[TX] Framed: {len(framed)}B → {TOPIC_CMD}")

    # MQTT
    response_received = [False]
    response_data = [None]

    def on_connect(client, userdata, flags, rc, props=None):
        client.subscribe(TOPIC_RSP)

    def on_message(client, userdata, msg):
        print(f"\n[RX] {msg.topic}: {len(msg.payload)}B")

        # Deframe
        result = frame_decode(msg.payload)
        if result is None:
            print("[RX] Frame decode FAILED (bad magic/CRC)")
            response_received[0] = True
            return

        payload, flags, stream_id = result
        print(f"[RX] Deframed: {len(payload)}B, flags=0x{flags:02X}, sid={stream_id}")

        # Decrypt
        if crypto:
            try:
                plaintext = crypto.decrypt(payload)
                print(f"[RX] Decrypted: {len(plaintext)}B")
            except Exception as e:
                print(f"[RX] Decrypt FAILED: {e}")
                # Try plaintext fallback
                plaintext = payload
        else:
            plaintext = payload

        # Decode response
        try:
            if args.binary:
                # Binary codec: [cluster][commandId][status][dataLen][data...]
                if len(plaintext) >= 4:
                    rsp_cluster = plaintext[0]
                    rsp_cmd = plaintext[1]
                    rsp_status = plaintext[2]
                    rsp_data_len = plaintext[3]
                    rsp_data = plaintext[4:4 + rsp_data_len] if rsp_data_len > 0 else b""
                    status_name = STATUS_NAMES.get(rsp_status, f"0x{rsp_status:02X}")
                    print(f"[RX] Response: cluster=0x{rsp_cluster:02X} "
                          f"cmd=0x{rsp_cmd:02X} status={status_name}")
                    if rsp_data:
                        print(f"[RX] Payload ({len(rsp_data)}B): {rsp_data.hex()}")
                        if len(rsp_data) == 4:
                            val = struct.unpack("<I", rsp_data)[0]
                            print(f"[RX] As uint32: {val}")
                else:
                    print(f"[RX] Binary too short: {plaintext.hex()}")
            else:
                rsp = decode_cmd_response(plaintext)
                status_name = STATUS_NAMES.get(rsp["status"], f"0x{rsp['status']:02X}")
                print(f"[RX] Response: cluster=0x{rsp['cluster']:02X} "
                      f"cmd=0x{rsp['command']:02X} status={status_name}")
                if rsp["payload"]:
                    print(f"[RX] Payload ({len(rsp['payload'])}B): "
                          f"{rsp['payload'].hex()}")
                    if len(rsp["payload"]) == 4:
                        val = struct.unpack("<I", rsp["payload"])[0]
                        print(f"[RX] As uint32: {val}")
                    elif len(rsp["payload"]) > 4:
                        try:
                            print(f"[RX] As string: {rsp['payload'].decode('utf-8', errors='replace')}")
                        except:
                            pass
        except Exception as e:
            print(f"[RX] Decode FAILED: {e}")
            print(f"[RX] Raw: {plaintext.hex()}")

        response_received[0] = True

    import ssl
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                         client_id="arcana_crypto_test",
                         transport="websockets")
    client.username_pw_set("arcana", "arcana")
    client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
    client.ws_set_options(path="/mqtt")
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"\nConnecting to {args.broker}:{args.port} (WSS)...")
    client.connect(args.broker, args.port, 60)
    client.loop_start()
    time.sleep(1)  # wait for subscribe

    # Publish command
    client.publish(TOPIC_CMD, framed, qos=0)
    print("Published. Waiting for response...")

    # Wait for response
    deadline = time.time() + args.timeout
    while not response_received[0] and time.time() < deadline:
        time.sleep(0.1)

    if not response_received[0]:
        print(f"\n[TIMEOUT] No response after {args.timeout}s")

    client.loop_stop()
    client.disconnect()

if __name__ == "__main__":
    main()
