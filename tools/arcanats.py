#!/usr/bin/env python3
"""
ArcanaTS v2 reader — parse .ats files from embedded devices.

Supports both plaintext and encrypted header formats.

Usage:
  python arcanats.py info  data.ats                                       # plaintext header
  python arcanats.py info  data.ats --header-key FLEET_KEY                # encrypted header
  python arcanats.py read  data.ats --key KEY                             # plaintext header
  python arcanats.py read  data.ats --key KEY --header-key FLEET_KEY      # encrypted header
  python arcanats.py read  data.ats --secret SECRET --header-key FLEET_KEY # derive key from secret+UID
"""

import struct
import sys
import argparse
from pathlib import Path

# -- Constants ---------------------------------------------------------------

BLOCK_SIZE = 4096
BLOCK_HEADER_SIZE = 32
BLOCK_PAYLOAD_SIZE = BLOCK_SIZE - BLOCK_HEADER_SIZE  # 4064
MAX_CHANNELS = 8
MULTI_CHANNEL_ID = 0xFF
ATS_FLAG_ENC_HEADER = 0x0010

# -- ChaCha20 (RFC 7539) ----------------------------------------------------

def _rotl32(v, n):
    return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF

def _quarter_round(s, a, b, c, d):
    s[a] = (s[a] + s[b]) & 0xFFFFFFFF; s[d] ^= s[a]; s[d] = _rotl32(s[d], 16)
    s[c] = (s[c] + s[d]) & 0xFFFFFFFF; s[b] ^= s[c]; s[b] = _rotl32(s[b], 12)
    s[a] = (s[a] + s[b]) & 0xFFFFFFFF; s[d] ^= s[a]; s[d] = _rotl32(s[d], 8)
    s[c] = (s[c] + s[d]) & 0xFFFFFFFF; s[b] ^= s[c]; s[b] = _rotl32(s[b], 7)

def chacha20_block(key: bytes, nonce: bytes, counter: int) -> bytes:
    """Generate one 64-byte keystream block."""
    assert len(key) == 32 and len(nonce) == 12
    k = struct.unpack('<8I', key)
    n = struct.unpack('<3I', nonce)
    state = [
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7],
        counter & 0xFFFFFFFF, n[0], n[1], n[2],
    ]
    working = list(state)
    for _ in range(10):
        _quarter_round(working, 0, 4, 8, 12)
        _quarter_round(working, 1, 5, 9, 13)
        _quarter_round(working, 2, 6, 10, 14)
        _quarter_round(working, 3, 7, 11, 15)
        _quarter_round(working, 0, 5, 10, 15)
        _quarter_round(working, 1, 6, 11, 12)
        _quarter_round(working, 2, 7, 8, 13)
        _quarter_round(working, 3, 4, 9, 14)
    out = b''
    for i in range(16):
        out += struct.pack('<I', (working[i] + state[i]) & 0xFFFFFFFF)
    return out

def chacha20_crypt(key: bytes, nonce: bytes, counter: int, data: bytearray):
    """Encrypt/decrypt data in-place."""
    offset = 0
    blk_ctr = counter
    while offset < len(data):
        ks = chacha20_block(key, nonce, blk_ctr)
        chunk = min(64, len(data) - offset)
        for i in range(chunk):
            data[offset + i] ^= ks[i]
        offset += chunk
        blk_ctr += 1

def chacha20_derive_key(secret: bytes, uid: bytes) -> bytes:
    """Derive device_key = ChaCha20(secret, uid_as_nonce, counter=0)[:32]."""
    assert len(secret) == 32 and len(uid) == 12
    buf = bytearray(32)
    chacha20_crypt(secret, uid, 0, buf)
    return bytes(buf)

# -- CRC-32 -----------------------------------------------------------------

def crc32_ieee(data: bytes) -> int:
    """IEEE 802.3 CRC-32 (same as zlib.crc32)."""
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF

# -- Field types -------------------------------------------------------------

FIELD_TYPE_NAMES = {
    0: 'U8', 1: 'U16', 2: 'U32', 3: 'I16', 4: 'I32',
    5: 'F32', 6: 'I24', 7: 'U64', 8: 'BYTES',
}

# -- File header parsing -----------------------------------------------------

def parse_file_header(data: bytes):
    """Parse 64-byte global header (expects "ATS2" at offset 0)."""
    if len(data) < 64:
        return None
    magic = data[0:4]
    if magic != b'ATS2':
        return None
    hdr = {}
    hdr['magic'] = magic.decode()
    hdr['version'] = data[4]
    hdr['headerBlocks'] = data[5]
    hdr['flags'] = struct.unpack_from('<H', data, 6)[0]
    hdr['cipherType'] = data[8]
    hdr['channelCount'] = data[9]
    hdr['overflowPolicy'] = data[10]
    hdr['deviceUidSize'] = data[11]
    hdr['createdEpoch'] = struct.unpack_from('<I', data, 12)[0]
    hdr['deviceUid'] = data[16:32].hex()
    hdr['totalBlockCount'] = struct.unpack_from('<I', data, 32)[0]
    hdr['lastSeqNo'] = struct.unpack_from('<I', data, 36)[0]
    hdr['indexBlockOffset'] = struct.unpack_from('<I', data, 40)[0]
    hdr['headerCrc32'] = struct.unpack_from('<I', data, 44)[0]
    return hdr

def parse_channel_descriptor(data: bytes):
    """Parse 32-byte channel descriptor."""
    ch = {}
    ch['channelId'] = data[0]
    ch['fieldCount'] = data[1]
    ch['recordSize'] = struct.unpack_from('<H', data, 2)[0]
    ch['sampleRateHz'] = struct.unpack_from('<H', data, 4)[0]
    ch['recordCount'] = struct.unpack_from('<H', data, 6)[0]
    ch['name'] = data[8:32].split(b'\x00')[0].decode('utf-8', errors='replace')
    return ch

def parse_field_desc(data: bytes):
    """Parse 16-byte field descriptor."""
    f = {}
    f['name'] = data[0:8].split(b'\x00')[0].decode('utf-8', errors='replace')
    f['type'] = data[8]
    f['typeName'] = FIELD_TYPE_NAMES.get(data[8], f'?{data[8]}')
    f['offset'] = data[9]
    f['scaleNum'] = struct.unpack_from('<H', data, 10)[0]
    f['scaleDen'] = struct.unpack_from('<H', data, 12)[0]
    return f

def parse_block_header(data: bytes):
    """Parse 32-byte data block header."""
    hdr = {}
    hdr['blockSeqNo'] = struct.unpack_from('<I', data, 0)[0]
    hdr['channelId'] = data[4]
    hdr['flags'] = data[5]
    hdr['recordCount'] = struct.unpack_from('<H', data, 6)[0]
    hdr['firstTimestamp'] = struct.unpack_from('<I', data, 8)[0]
    hdr['lastTimestamp'] = struct.unpack_from('<I', data, 12)[0]
    hdr['nonce'] = data[16:28]
    hdr['payloadCrc32'] = struct.unpack_from('<I', data, 28)[0]
    return hdr

# -- Field value extraction --------------------------------------------------

def read_field_value(record: bytes, field: dict):
    """Extract a typed field value from a record."""
    off = field['offset']
    t = field['type']
    try:
        if t == 0:  # U8
            return record[off]
        elif t == 1:  # U16
            return struct.unpack_from('<H', record, off)[0]
        elif t == 2:  # U32
            return struct.unpack_from('<I', record, off)[0]
        elif t == 3:  # I16
            return struct.unpack_from('<h', record, off)[0]
        elif t == 4:  # I32
            return struct.unpack_from('<i', record, off)[0]
        elif t == 5:  # F32
            return struct.unpack_from('<f', record, off)[0]
        elif t == 6:  # I24
            b = record[off:off+3]
            v = b[0] | (b[1] << 8) | (b[2] << 16)
            if v & 0x800000:
                v -= 0x1000000
            return v
        elif t == 7:  # U64
            return struct.unpack_from('<Q', record, off)[0]
        elif t == 8:  # BYTES
            size = field['scaleNum']
            return record[off:off+size].hex()
    except (struct.error, IndexError):
        return None
    return None

# -- Main commands -----------------------------------------------------------

class AtsReader:
    def __init__(self, path: str, key_hex: str = None, header_key_hex: str = None,
                 secret_hex: str = None):
        self.path = path
        self.header_key = bytes.fromhex(header_key_hex) if header_key_hex else None
        self.data = Path(path).read_bytes()
        self.file_hdr = None
        self.channels = {}    # channelId -> descriptor
        self.fields = {}      # channelId -> [field_desc]
        self.header_base = 0  # 0=plaintext, 16=encrypted
        self.encrypted_header = False

        self._parse_header()

        # Resolve data key: --key takes precedence, --secret derives from UID
        if key_hex:
            self.key = bytes.fromhex(key_hex)
        elif secret_hex and self.file_hdr:
            uid_hex = self.file_hdr['deviceUid'][:self.file_hdr['deviceUidSize'] * 2]
            uid = bytes.fromhex(uid_hex)
            secret = bytes.fromhex(secret_hex)
            self.key = chacha20_derive_key(secret, uid)
        else:
            self.key = None

    def _parse_header(self):
        if len(self.data) < BLOCK_SIZE:
            raise ValueError(f"File too small ({len(self.data)} bytes)")

        header_block = bytearray(self.data[:BLOCK_SIZE])

        # Detect format: plaintext ("ATS2" at offset 0) vs encrypted
        if header_block[0:4] == b'ATS2':
            # Legacy plaintext format
            self.header_base = 0
            self.encrypted_header = False
        elif self.header_key:
            # Encrypted header: nonce at [0..11], encrypted data at [16..4095]
            nonce = bytes(header_block[0:12])
            enc_part = bytearray(header_block[16:])
            chacha20_crypt(self.header_key, nonce, 0, enc_part)
            header_block[16:] = enc_part  # write decrypted data back

            # Validate: "ATS2" magic at [16]
            if header_block[16:20] == b'ATS2':
                self.header_base = 16
                self.encrypted_header = True
            else:
                raise ValueError(
                    "Decryption failed: wrong header key or corrupted header. "
                    "Decrypted magic bytes do not match 'ATS2'.")
        else:
            raise ValueError(
                "Not a plaintext ATS2 file (no 'ATS2' magic at offset 0). "
                "Use --header-key to decrypt an encrypted header.")

        base = self.header_base

        self.file_hdr = parse_file_header(bytes(header_block[base:base+64]))
        if not self.file_hdr:
            raise ValueError("Invalid ATS2 file header after decryption")

        # Validate CRC
        expected_crc = crc32_ieee(bytes(header_block[base:base+44]))
        if expected_crc != self.file_hdr['headerCrc32']:
            raise ValueError("Header CRC mismatch — corrupted or wrong key")

        # Parse channel descriptors
        for i in range(MAX_CHANNELS):
            off = base + 0x0040 + i * 32
            if off + 32 > BLOCK_SIZE:
                break
            ch = parse_channel_descriptor(bytes(header_block[off:off+32]))
            if ch['channelId'] == 0xFF:
                continue
            self.channels[ch['channelId']] = ch

            # Parse field table
            ft_off = base + 0x0140 + i * 256
            fields = []
            for j in range(ch['fieldCount']):
                fd_off = ft_off + j * 16
                if fd_off + 16 > BLOCK_SIZE:
                    break
                fd = parse_field_desc(bytes(header_block[fd_off:fd_off+16]))
                fields.append(fd)
            self.fields[ch['channelId']] = fields

    def info(self):
        """Print file header and channel info."""
        h = self.file_hdr
        print(f"=== {self.path} ===")
        print(f"Magic: {h['magic']}  Version: {h['version']}")
        print(f"Cipher: {h['cipherType']}  Channels: {h['channelCount']}")
        flags = []
        if h['flags'] & 0x01: flags.append('encrypted')
        if h['flags'] & 0x02: flags.append('has_index')
        if h['flags'] & 0x04: flags.append('has_hmac')
        if h['flags'] & 0x08: flags.append('has_shadow')
        if h['flags'] & 0x10: flags.append('enc_header')
        print(f"Flags: {' '.join(flags) or 'none'} (0x{h['flags']:04X})")
        print(f"Created: {h['createdEpoch']}  UID: {h['deviceUid'][:h['deviceUidSize']*2]}")
        print(f"Blocks: {h['totalBlockCount']}  LastSeq: {h['lastSeqNo']}")
        overflow = 'BLOCK' if h['overflowPolicy'] == 0 else 'DROP'
        print(f"Overflow: {overflow}")
        if self.encrypted_header:
            print(f"Header: ENCRYPTED (nonce: {self.data[:12].hex()})")
        else:
            print(f"Header: plaintext")
        print()

        for chId, ch in sorted(self.channels.items()):
            print(f"Channel {chId}: {ch['name']} ({ch['recordSize']} bytes/rec, "
                  f"{ch['fieldCount']} fields, {ch['sampleRateHz']} Hz)")
            for f in self.fields.get(chId, []):
                scale = ''
                if f['scaleNum'] != 1 or f['scaleDen'] != 1:
                    scale = f" *{f['scaleNum']}/{f['scaleDen']}"
                print(f"  [{f['offset']:3d}] {f['name']:8s} {f['typeName']}{scale}")
            print()

        # Count data blocks
        n_blocks = (len(self.data) - BLOCK_SIZE) // BLOCK_SIZE
        print(f"Data blocks in file: {n_blocks}")
        print(f"File size: {len(self.data)} bytes")

    def read_records(self, channel_filter=None, schema_filter=None):
        """Iterate all records, yielding (channelId, channel_name, field_values_dict)."""
        # Resolve schema filter to channel ID
        if schema_filter:
            for chId, ch in self.channels.items():
                if ch['name'] == schema_filter:
                    channel_filter = chId
                    break
            else:
                print(f"Schema '{schema_filter}' not found", file=sys.stderr)
                return

        offset = BLOCK_SIZE  # skip header block
        while offset + BLOCK_SIZE <= len(self.data):
            block_data = bytearray(self.data[offset:offset + BLOCK_SIZE])
            bhdr = parse_block_header(block_data[:BLOCK_HEADER_SIZE])

            # Validate block
            if bhdr['blockSeqNo'] == 0 or bhdr['blockSeqNo'] == 0xFFFFFFFF:
                offset += BLOCK_SIZE
                continue  # skip uncommitted block

            payload = block_data[BLOCK_HEADER_SIZE:]

            # Verify CRC of encrypted payload
            enc_crc = crc32_ieee(bytes(payload))
            if enc_crc != bhdr['payloadCrc32']:
                print(f"CRC mismatch at block {offset//BLOCK_SIZE}: "
                      f"got 0x{enc_crc:08X}, expected 0x{bhdr['payloadCrc32']:08X}",
                      file=sys.stderr)
                offset += BLOCK_SIZE
                continue

            # Decrypt
            if self.key and self.file_hdr['cipherType'] != 0:
                chacha20_crypt(self.key, bytes(bhdr['nonce']), 0, payload)

            if bhdr['channelId'] != MULTI_CHANNEL_ID:
                # Single-channel block
                chId = bhdr['channelId']
                if channel_filter is not None and chId != channel_filter:
                    offset += BLOCK_SIZE
                    continue
                if chId not in self.channels:
                    offset += BLOCK_SIZE
                    continue

                recSize = self.channels[chId]['recordSize']
                fields = self.fields.get(chId, [])
                ch_name = self.channels[chId]['name']

                for r in range(bhdr['recordCount']):
                    rec = payload[r * recSize : (r+1) * recSize]
                    vals = {}
                    for f in fields:
                        vals[f['name']] = read_field_value(rec, f)
                    yield chId, ch_name, vals
            else:
                # Multi-channel block: tagged records
                pos = 0
                while pos < BLOCK_PAYLOAD_SIZE:
                    tag_chId = payload[pos]
                    if tag_chId >= MAX_CHANNELS or tag_chId not in self.channels:
                        break
                    recSize = self.channels[tag_chId]['recordSize']
                    rec = payload[pos+1 : pos+1+recSize]

                    if channel_filter is None or tag_chId == channel_filter:
                        fields = self.fields.get(tag_chId, [])
                        ch_name = self.channels[tag_chId]['name']
                        vals = {}
                        for f in fields:
                            vals[f['name']] = read_field_value(rec, f)
                        yield tag_chId, ch_name, vals

                    pos += 1 + recSize

            offset += BLOCK_SIZE

# -- CLI ---------------------------------------------------------------------

def cmd_info(args):
    reader = AtsReader(args.file, header_key_hex=args.header_key)
    reader.info()

def cmd_read(args):
    reader = AtsReader(args.file, key_hex=args.key,
                       header_key_hex=args.header_key,
                       secret_hex=args.secret)

    first = True
    for chId, ch_name, vals in reader.read_records(
            channel_filter=args.channel, schema_filter=args.schema):
        if first:
            # Print CSV header
            field_names = list(vals.keys())
            print(f"channel,schema,{','.join(field_names)}")
            first = False
        values = [str(v) if v is not None else '' for v in vals.values()]
        print(f"{chId},{ch_name},{','.join(values)}")

    if first:
        print("No records found.", file=sys.stderr)

def main():
    parser = argparse.ArgumentParser(description='ArcanaTS v2 file reader')
    sub = parser.add_subparsers(dest='cmd')

    # Common args
    def add_common_args(p):
        p.add_argument('file', help='.ats file path')
        p.add_argument('--header-key',
                       help='Fleet master key (64 hex chars) for encrypted header')

    p_info = sub.add_parser('info', help='Show file header and channel info')
    add_common_args(p_info)

    p_read = sub.add_parser('read', help='Read and decrypt records')
    add_common_args(p_read)
    p_read.add_argument('--key', help='32-byte hex device key')
    p_read.add_argument('--secret',
                        help='Fleet master secret (64 hex chars) — derives key from UID in file')
    p_read.add_argument('--schema', help='Filter by schema name (e.g. MPU6050)')
    p_read.add_argument('--channel', type=int, help='Filter by channel ID')

    args = parser.parse_args()
    if not args.cmd:
        parser.print_help()
        return

    try:
        if args.cmd == 'info':
            cmd_info(args)
        elif args.cmd == 'read':
            cmd_read(args)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
