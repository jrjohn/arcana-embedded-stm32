/**
 * @file protobuf.js
 * @brief Manual protobuf codec for CmdRequest / CmdResponse
 *
 * No .proto compiler dependency — matches arcana_cmd.proto:
 *
 *   message CmdRequest {
 *     uint32 cluster = 1;
 *     uint32 command = 2;
 *     bytes  payload = 3;   // max 128
 *   }
 *
 *   message CmdResponse {
 *     uint32 cluster = 1;
 *     uint32 command = 2;
 *     uint32 status  = 3;
 *     bytes  payload = 4;   // max 256
 *   }
 */

// ─── Varint encode/decode ───────────────────────────────────────────────────

function encodeVarint(value) {
  const out = [];
  while (value > 0x7F) {
    out.push((value & 0x7F) | 0x80);
    value >>>= 7;
  }
  out.push(value & 0x7F);
  return Buffer.from(out);
}

function decodeVarint(data, offset) {
  let result = 0;
  let shift = 0;
  while (offset < data.length) {
    const b = data[offset++];
    result |= (b & 0x7F) << shift;
    if ((b & 0x80) === 0) return [result >>> 0, offset];
    shift += 7;
    if (shift > 35) throw new Error('Varint too long');
  }
  throw new Error('Truncated varint');
}

// ─── Field encoders ─────────────────────────────────────────────────────────

function encodeUint32Field(fieldNumber, value) {
  if (value === 0) return Buffer.alloc(0); // proto3: omit default
  const tag = encodeVarint((fieldNumber << 3) | 0); // wire type 0 = varint
  const val = encodeVarint(value);
  return Buffer.concat([tag, val]);
}

function encodeBytesField(fieldNumber, data) {
  if (!data || data.length === 0) return Buffer.alloc(0);
  const tag = encodeVarint((fieldNumber << 3) | 2); // wire type 2 = length-delimited
  const len = encodeVarint(data.length);
  return Buffer.concat([tag, len, data]);
}

// ─── CmdRequest encoder ────────────────────────────────────────────────────

/**
 * Encode CmdRequest protobuf.
 * @param {number} cluster
 * @param {number} command
 * @param {Buffer} [payload]
 * @returns {Buffer}
 */
export function encodeCmdRequest(cluster, command, payload = Buffer.alloc(0)) {
  return Buffer.concat([
    encodeUint32Field(1, cluster),
    encodeUint32Field(2, command),
    encodeBytesField(3, payload),
  ]);
}

// ─── CmdResponse decoder ───────────────────────────────────────────────────

/**
 * Decode CmdResponse protobuf.
 * @param {Buffer} data
 * @returns {{ cluster: number, command: number, status: number, payload: Buffer }}
 */
export function decodeCmdResponse(data) {
  const result = { cluster: 0, command: 0, status: 0, payload: Buffer.alloc(0) };
  let offset = 0;

  while (offset < data.length) {
    const [tag, off1] = decodeVarint(data, offset);
    offset = off1;
    const fieldNumber = tag >>> 3;
    const wireType = tag & 0x07;

    if (wireType === 0) {
      // Varint
      const [value, off2] = decodeVarint(data, offset);
      offset = off2;
      if (fieldNumber === 1) result.cluster = value;
      else if (fieldNumber === 2) result.command = value;
      else if (fieldNumber === 3) result.status = value;
    } else if (wireType === 2) {
      // Length-delimited
      const [length, off2] = decodeVarint(data, offset);
      offset = off2;
      const value = data.subarray(offset, offset + length);
      offset += length;
      if (fieldNumber === 4) result.payload = Buffer.from(value);
    } else {
      throw new Error(`Unsupported wire type ${wireType}`);
    }
  }

  return result;
}
