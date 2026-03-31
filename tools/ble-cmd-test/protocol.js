/**
 * @file protocol.js
 * @brief Arcana wire protocol — CRC-16, FrameCodec, FrameAssembler, CommandCodec
 *
 * Ported from STM32 C++ headers:
 *   Shared/Inc/Crc16.hpp, FrameCodec.hpp, FrameAssembler.hpp
 *   Targets/stm32f051c8/Services/command/CommandCodec.hpp
 */

// ─── CRC-16 (polynomial 0x8408, matches esp_crc16_le) ──────────────────────

export function crc16(init, data, offset = 0, length = data.length - offset) {
  let crc = init;
  for (let i = offset; i < offset + length; i++) {
    crc ^= data[i];
    for (let bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) {
        crc = (crc >>> 1) ^ 0x8408;
      } else {
        crc >>>= 1;
      }
    }
    crc &= 0xFFFF;
  }
  return crc;
}

// ─── FrameCodec ─────────────────────────────────────────────────────────────

export const MAGIC0 = 0xAC;
export const MAGIC1 = 0xDA;
export const VERSION = 0x01;
export const FLAG_FIN = 0x01;
export const SID_NONE = 0x00;
export const OVERHEAD = 9;  // 7 header + 2 CRC
export const HEADER_SIZE = 7;

/**
 * Build a framed buffer from payload.
 * @param {Buffer} payload
 * @param {number} flags
 * @param {number} streamId
 * @returns {Buffer} complete frame
 */
export function frame(payload, flags = FLAG_FIN, streamId = SID_NONE) {
  const payloadLen = payload.length;
  const totalLen = payloadLen + OVERHEAD;
  const buf = Buffer.alloc(totalLen);

  // Header
  buf[0] = MAGIC0;
  buf[1] = MAGIC1;
  buf[2] = VERSION;
  buf[3] = flags;
  buf[4] = streamId;
  buf[5] = payloadLen & 0xFF;
  buf[6] = (payloadLen >>> 8) & 0xFF;

  // Payload
  payload.copy(buf, HEADER_SIZE);

  // CRC-16 over header + payload
  const crc = crc16(0, buf, 0, HEADER_SIZE + payloadLen);
  buf[HEADER_SIZE + payloadLen] = crc & 0xFF;
  buf[HEADER_SIZE + payloadLen + 1] = (crc >>> 8) & 0xFF;

  return buf;
}

/**
 * Parse a framed buffer — validate magic, version, CRC.
 * @param {Buffer} frameBuf
 * @returns {{ payload: Buffer, flags: number, streamId: number } | null}
 */
export function deframe(frameBuf) {
  if (frameBuf.length < OVERHEAD) return null;

  if (frameBuf[0] !== MAGIC0 || frameBuf[1] !== MAGIC1) return null;
  if (frameBuf[2] !== VERSION) return null;

  const payloadLen = frameBuf[5] | (frameBuf[6] << 8);
  if (HEADER_SIZE + payloadLen + 2 !== frameBuf.length) return null;

  // CRC check
  const expected = crc16(0, frameBuf, 0, HEADER_SIZE + payloadLen);
  const received = frameBuf[HEADER_SIZE + payloadLen] |
                   (frameBuf[HEADER_SIZE + payloadLen + 1] << 8);
  if (expected !== received) return null;

  return {
    payload: frameBuf.subarray(HEADER_SIZE, HEADER_SIZE + payloadLen),
    flags: frameBuf[3],
    streamId: frameBuf[4],
  };
}

// ─── FrameAssembler (byte-level state machine for BLE MTU fragmentation) ───

const State = { IDLE: 0, MAGIC1: 1, HEADER: 2, PAYLOAD: 3, COMPLETE: 4 };

export class FrameAssembler {
  constructor(maxFrame = 64) {
    this.maxFrame = maxFrame;
    this.buf = Buffer.alloc(maxFrame);
    this.reset();
  }

  reset() {
    this.state = State.IDLE;
    this.pos = 0;
    this.frameLen = 0;
    this.headerLeft = 0;
    this.remaining = 0;
  }

  /**
   * Feed one byte. Returns a Buffer when a complete frame is assembled, null otherwise.
   */
  feedByte(b) {
    switch (this.state) {
      case State.IDLE:
        if (b === MAGIC0) {
          this.buf[0] = b;
          this.pos = 1;
          this.state = State.MAGIC1;
        }
        break;

      case State.MAGIC1:
        if (b === MAGIC1) {
          this.buf[this.pos++] = b;
          this.state = State.HEADER;
          this.headerLeft = 5; // ver + flags + sid + lenLo + lenHi
        } else if (b === MAGIC0) {
          this.buf[0] = b;
          this.pos = 1;
        } else {
          this.reset();
        }
        break;

      case State.HEADER:
        if (this.pos >= this.maxFrame) { this.reset(); break; }
        this.buf[this.pos++] = b;
        this.headerLeft--;
        if (this.headerLeft === 0) {
          const payloadLen = this.buf[5] | (this.buf[6] << 8);
          this.remaining = payloadLen + 2; // payload + 2 CRC bytes
          if (this.pos + this.remaining > this.maxFrame) {
            this.reset();
          } else {
            this.state = State.PAYLOAD;
          }
        }
        break;

      case State.PAYLOAD:
        this.buf[this.pos++] = b;
        this.remaining--;
        if (this.remaining === 0) {
          this.frameLen = this.pos;
          this.state = State.COMPLETE;
          const result = Buffer.from(this.buf.subarray(0, this.frameLen));
          this.reset();
          return result;
        }
        break;

      case State.COMPLETE:
        // Should have been consumed — reset and retry
        this.reset();
        if (b === MAGIC0) {
          this.buf[0] = b;
          this.pos = 1;
          this.state = State.MAGIC1;
        }
        break;
    }
    return null;
  }
}

// ─── CommandCodec ───────────────────────────────────────────────────────────

export const Cluster = {
  System:   0x00,
  Sensor:   0x01,
  Device:   0x02,
  Security: 0x04,
};

export const CommandStatus = {
  Success:      0,
  NotFound:     1,
  InvalidParam: 2,
  Busy:         3,
  Error:        4,
  AuthRequired: 5,
};

export const CommandStatusName = Object.fromEntries(
  Object.entries(CommandStatus).map(([k, v]) => [v, k])
);

/**
 * Encode a command request payload.
 * @param {number} cluster
 * @param {number} commandId
 * @param {Buffer} [params]
 * @returns {Buffer} payload (3 + params.length bytes)
 */
export function encodeRequest(cluster, commandId, params = Buffer.alloc(0)) {
  const buf = Buffer.alloc(3 + params.length);
  buf[0] = cluster;
  buf[1] = commandId;
  buf[2] = params.length;
  params.copy(buf, 3);
  return buf;
}

/**
 * Decode a command response payload.
 * @param {Buffer} payload
 * @returns {{ cluster: number, commandId: number, status: number, statusName: string, data: Buffer } | null}
 */
export function decodeResponse(payload) {
  if (payload.length < 4) return null;
  const cluster = payload[0];
  const commandId = payload[1];
  const status = payload[2];
  const dataLen = payload[3];
  const data = payload.subarray(4, 4 + dataLen);
  return {
    cluster,
    commandId,
    status,
    statusName: CommandStatusName[status] || `Unknown(${status})`,
    data,
  };
}
