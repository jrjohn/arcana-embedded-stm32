#!/usr/bin/env python3
"""
OTA Test Server — serves firmware.bin for STM32 OTA download testing.

Usage:
  1. Build the app: cd Targets/stm32f103ze/Debug && make all
  2. Start this server: python3 test/ota_test_server.py
  3. Trigger OTA from device (MQTT or serial command)
  4. Device downloads firmware.bin from this server

The server:
- Computes CRC-32 of the .bin file
- Generates ota_meta.bin
- Serves both files via HTTP on port 8080
- Prints the OTA trigger command for MQTT
"""

import http.server
import struct
import sys
import os
import binascii

# Default firmware path
DEFAULT_FW = "Targets/stm32f103ze/Debug/arcana-embedded-f103.bin"
PORT = 8080

# OTA metadata constants (must match ota_header.h)
OTA_META_MAGIC = 0x41524F54  # "AROT"
OTA_META_VERSION = 1
APP_FLASH_BASE = 0x08008000


def crc32_ieee(data: bytes) -> int:
    """Standard CRC-32 IEEE (same as zlib.crc32)"""
    return binascii.crc32(data) & 0xFFFFFFFF


def create_ota_meta(fw_path: str) -> bytes:
    """Create ota_meta.bin content from firmware binary"""
    with open(fw_path, "rb") as f:
        fw_data = f.read()

    fw_size = len(fw_data)
    fw_crc32 = crc32_ieee(fw_data)

    # Pack metadata (44 bytes, little-endian)
    # magic(4) + version(1) + reserved(3) + fw_size(4) + crc32(4) +
    # target_addr(4) + fw_version(16) + timestamp(4) + meta_crc(4)
    version_str = b"ota-test\x00" + b"\x00" * 7  # 16 bytes
    meta_body = struct.pack("<IBBBI16sI",
                            OTA_META_MAGIC,      # magic
                            OTA_META_VERSION,     # version
                            0, 0, 0,              # reserved[3]
                            fw_size,              # fw_size — NOT HERE
                            fw_crc32,             # crc32
                            APP_FLASH_BASE,       # target_addr
                            version_str,          # fw_version[16]
                            0)                    # timestamp

    # Actually, let me pack it correctly matching the struct layout
    meta_no_crc = struct.pack("<I3BI16sI",
                              OTA_META_MAGIC,
                              OTA_META_VERSION, 0, 0, 0,  # version + reserved[3]
                              fw_size,
                              fw_crc32,
                              APP_FLASH_BASE,
                              version_str,
                              0)  # timestamp

    # Hmm, the struct is packed. Let me be explicit:
    meta = bytearray(44)
    struct.pack_into("<I", meta, 0, OTA_META_MAGIC)       # magic
    meta[4] = OTA_META_VERSION                              # version
    meta[5] = 0; meta[6] = 0; meta[7] = 0                 # reserved[3]
    struct.pack_into("<I", meta, 8, fw_size)               # fw_size
    struct.pack_into("<I", meta, 12, fw_crc32)             # crc32
    struct.pack_into("<I", meta, 16, APP_FLASH_BASE)       # target_addr
    meta[20:36] = version_str                               # fw_version[16]
    struct.pack_into("<I", meta, 36, 0)                    # timestamp

    # meta_crc: CRC-32 of first 40 bytes
    meta_crc = crc32_ieee(bytes(meta[:40]))
    struct.pack_into("<I", meta, 40, meta_crc)

    return bytes(meta), fw_size, fw_crc32


def main():
    fw_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_FW

    # Find project root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    fw_full_path = os.path.join(project_root, fw_path) if not os.path.isabs(fw_path) else fw_path

    if not os.path.exists(fw_full_path):
        print(f"Error: {fw_full_path} not found")
        print(f"Build the app first: cd Targets/stm32f103ze/Debug && make all")
        sys.exit(1)

    fw_size = os.path.getsize(fw_full_path)
    meta_data, _, fw_crc32 = create_ota_meta(fw_full_path)

    # Write ota_meta.bin next to firmware
    meta_path = os.path.join(os.path.dirname(fw_full_path), "ota_meta.bin")
    with open(meta_path, "wb") as f:
        f.write(meta_data)

    print(f"Firmware: {fw_full_path}")
    print(f"Size: {fw_size} bytes")
    print(f"CRC-32: 0x{fw_crc32:08X}")
    print(f"Meta: {meta_path}")
    print()

    import socket
    hostname = socket.gethostbyname(socket.gethostname())

    print(f"=== OTA Server running on http://{hostname}:{PORT} ===")
    print()
    print("MQTT trigger command:")
    print(f'  mosquitto_pub -t arcana/ota -m \'{{"url":"http://{hostname}:{PORT}/firmware.bin","size":{fw_size},"crc32":"{fw_crc32:08X}"}}\'')
    print()

    # Serve files from firmware directory
    os.chdir(os.path.dirname(fw_full_path))

    # Rename .bin to firmware.bin for serving
    serve_name = "firmware.bin"
    if os.path.basename(fw_full_path) != serve_name:
        if os.path.exists(serve_name):
            os.remove(serve_name)
        os.symlink(os.path.basename(fw_full_path), serve_name)

    handler = http.server.SimpleHTTPRequestHandler
    with http.server.HTTPServer(("", PORT), handler) as httpd:
        print(f"Serving on port {PORT}...")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")


if __name__ == "__main__":
    main()
