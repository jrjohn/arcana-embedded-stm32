#!/usr/bin/env python3
"""Test device registration API with FrameCodec + protobuf"""

import sys
import os
import struct
import requests

# FrameCodec (same as registration_api.py)
MAGIC = b"\xAC\xDA"
VERSION = 0x01
FLAG_FIN = 0x01
SID_REGISTER = 0x10

def crc16(data, init=0):
    crc = init
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1: crc = (crc >> 1) ^ 0x8408
            else: crc >>= 1
    return crc & 0xFFFF

def frame_encode(payload, flags=FLAG_FIN, stream_id=0):
    header = MAGIC + bytes([VERSION, flags, stream_id])
    header += struct.pack("<H", len(payload))
    body = header + payload
    return body + struct.pack("<H", crc16(body))

def frame_decode(data):
    if len(data) < 9: return None
    if data[0:2] != MAGIC: return None
    flags, sid = data[3], data[4]
    plen = struct.unpack("<H", data[5:7])[0]
    if len(data) != 7 + plen + 2: return None
    return data[7:7+plen], flags, sid

def pb_encode_varint(v):
    r = b""
    while v > 0x7F: r += bytes([(v & 0x7F) | 0x80]); v >>= 7
    r += bytes([v & 0x7F])
    return r

def pb_field(fn, wt, data):
    tag = pb_encode_varint((fn << 3) | wt)
    if wt == 0: return tag + pb_encode_varint(data)
    elif wt == 2: return tag + pb_encode_varint(len(data)) + data
    return tag

def pb_decode(data):
    fields = {}
    off = 0
    while off < len(data):
        b = data[off]; off += 1
        fn, wt = b >> 3, b & 7
        if wt == 0:
            v = 0; s = 0
            while True:
                b = data[off]; off += 1; v |= (b&0x7F)<<s; s += 7
                if not (b&0x80): break
            fields[fn] = v
        elif wt == 2:
            l = 0; s = 0
            while True:
                b = data[off]; off += 1; l |= (b&0x7F)<<s; s += 7
                if not (b&0x80): break
            fields[fn] = data[off:off+l]; off += l
    return fields

# --- Test ---

SERVER = os.environ.get("REG_SERVER", "http://localhost:8080")

def test_register(device_id="TEST0001"):
    # Fake P-256 public key (64 bytes)
    fake_pubkey = os.urandom(64)

    # Encode RegisterRequest protobuf
    pb = b""
    pb += pb_field(1, 2, device_id.encode())  # device_id
    pb += pb_field(2, 2, fake_pubkey)          # public_key
    pb += pb_field(3, 0, 0x0100)               # firmware_ver

    # Wrap in FrameCodec
    frame = frame_encode(pb, stream_id=SID_REGISTER)

    print(f"[TEST] POST {SERVER}/api/register")
    print(f"  device_id: {device_id}")
    print(f"  pubkey: {fake_pubkey.hex()[:32]}...")
    print(f"  frame: {len(frame)} bytes")

    resp = requests.post(f"{SERVER}/api/register",
                         data=frame,
                         headers={"Content-Type": "application/octet-stream"})

    print(f"  HTTP {resp.status_code}")

    # Decode response frame
    result = frame_decode(resp.content)
    if not result:
        print(f"  ERROR: invalid frame response")
        print(f"  Raw: {resp.content[:100]}")
        return

    payload, flags, sid = result
    fields = pb_decode(payload)

    success = fields.get(1, 0)
    print(f"  success: {bool(success)}")
    if success:
        print(f"  mqtt_user: {fields.get(2, b'').decode()}")
        print(f"  mqtt_pass: {fields.get(3, b'').decode()}")
        print(f"  mqtt_broker: {fields.get(4, b'').decode()}")
        print(f"  mqtt_port: {fields.get(5, 0)}")
        print(f"  upload_token: {fields.get(6, b'').decode()[:40]}...")
        print(f"  topic_prefix: {fields.get(7, b'').decode()}")
    else:
        print(f"  error: {fields.get(8, b'').decode()}")

    # Test duplicate registration
    print(f"\n[TEST] Duplicate registration...")
    resp2 = requests.post(f"{SERVER}/api/register", data=frame,
                          headers={"Content-Type": "application/octet-stream"})
    print(f"  HTTP {resp2.status_code} (expect 409)")

    # Test device status
    print(f"\n[TEST] GET {SERVER}/api/device/{device_id}/status")
    resp3 = requests.get(f"{SERVER}/api/device/{device_id}/status")
    print(f"  {resp3.json()}")

if __name__ == "__main__":
    did = sys.argv[1] if len(sys.argv) > 1 else "TEST0001"
    test_register(did)
