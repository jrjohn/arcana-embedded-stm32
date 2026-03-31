#!/usr/bin/env node
/**
 * @file ble-sensor-monitor.js
 * @brief BLE sensor stream monitor — decodes ChaCha20 encrypted protobuf
 *
 * Connects to ArcanaBLE (HC-08) via native BLE GATT, receives encrypted
 * sensor frames (streamId=0x20), decrypts with ChaCha20, decodes protobuf.
 * Same format as MQTT sensor publish.
 *
 * Frame:  [magic:2][ver:1][flags:1][sid:1][len:2 LE][payload:N][crc:2 LE]
 * Payload: [nonce:12][encrypted_protobuf:N]
 * Protobuf: sint32 temp×10, sint32 ax, sint32 ay, sint32 az, uint32 als, uint32 ps
 *
 * Usage:
 *   node ble-sensor-monitor.js --uid 32FFD605...         # derive key from UID
 *   node ble-sensor-monitor.js --key <64-char-hex>       # explicit key
 *   node ble-sensor-monitor.js --json                    # output as JSON lines
 *   node ble-sensor-monitor.js --raw                     # also show raw hex
 */

import noble from '@abandonware/noble';

// ─── CLI Args ───────────────────────────────────────────────────────────────

const args = process.argv.slice(2);
const getArg = (f) => { const i = args.indexOf(f); return i >= 0 && i + 1 < args.length ? args[i + 1] : null; };
const hasFlag = (f) => args.includes(f);

const UID_HEX = getArg('--uid');
const KEY_HEX = getArg('--key');
const JSON_MODE = hasFlag('--json');
const RAW_MODE = hasFlag('--raw');

// ─── ChaCha20 (RFC 7539) ───────────────────────────────────────────────────

function rotl32(v, n) { return ((v << n) | (v >>> (32 - n))) >>> 0; }

function quarterRound(s, a, b, c, d) {
  s[a] = (s[a] + s[b]) >>> 0; s[d] ^= s[a]; s[d] = rotl32(s[d], 16);
  s[c] = (s[c] + s[d]) >>> 0; s[b] ^= s[c]; s[b] = rotl32(s[b], 12);
  s[a] = (s[a] + s[b]) >>> 0; s[d] ^= s[a]; s[d] = rotl32(s[d], 8);
  s[c] = (s[c] + s[d]) >>> 0; s[b] ^= s[c]; s[b] = rotl32(s[b], 7);
}

function chacha20Decrypt(key, nonce, data) {
  const out = Buffer.from(data);
  const state = new Uint32Array(16);
  const working = new Uint32Array(16);
  const ks = Buffer.alloc(64);

  const loadLE32 = (buf, off) => buf[off] | (buf[off+1]<<8) | (buf[off+2]<<16) | (buf[off+3]<<24);

  let offset = 0, counter = 0;
  while (offset < out.length) {
    state[0]=0x61707865; state[1]=0x3320646e; state[2]=0x79622d32; state[3]=0x6b206574;
    for (let i=0;i<8;i++) state[4+i] = loadLE32(key, i*4);
    state[12] = counter;
    for (let i=0;i<3;i++) state[13+i] = loadLE32(nonce, i*4);
    working.set(state);
    for (let i=0;i<10;i++) {
      quarterRound(working,0,4,8,12); quarterRound(working,1,5,9,13);
      quarterRound(working,2,6,10,14); quarterRound(working,3,7,11,15);
      quarterRound(working,0,5,10,15); quarterRound(working,1,6,11,12);
      quarterRound(working,2,7,8,13);  quarterRound(working,3,4,9,14);
    }
    for (let i=0;i<16;i++) {
      const v=(working[i]+state[i])>>>0;
      ks[i*4]=v&0xFF; ks[i*4+1]=(v>>>8)&0xFF; ks[i*4+2]=(v>>>16)&0xFF; ks[i*4+3]=(v>>>24)&0xFF;
    }
    const len = Math.min(64, out.length - offset);
    for (let i=0;i<len;i++) out[offset+i] ^= ks[i];
    counter++; offset += len;
  }
  return out;
}

// ─── Device Key Derivation ──────────────────────────────────────────────────

const LEGACY_FLEET_MASTER = Buffer.from([
  0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF, 0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10,
  0xA5,0x5A,0x0F,0xF0,0x12,0x34,0x56,0x78, 0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44,
]);

function deriveDeviceKey(fleetMaster, uid) {
  return chacha20Decrypt(fleetMaster, uid, Buffer.alloc(32));
}

// ─── CRC-16 (poly 0x8408) ──────────────────────────────────────────────────

function crc16(data, offset = 0, length = data.length) {
  let crc = 0;
  for (let i = offset; i < offset + length; i++) {
    crc ^= data[i];
    for (let b = 0; b < 8; b++) crc = (crc & 1) ? (crc >>> 1) ^ 0x8408 : crc >>> 1;
    crc &= 0xFFFF;
  }
  return crc;
}

// ─── FrameCodec deframe ─────────────────────────────────────────────────────

function deframe(buf) {
  if (buf.length < 9) return null;
  if (buf[0] !== 0xAC || buf[1] !== 0xDA || buf[2] !== 0x01) return null;
  const payloadLen = buf[5] | (buf[6] << 8);
  if (7 + payloadLen + 2 !== buf.length) return null;
  const expected = crc16(buf, 0, 7 + payloadLen);
  const received = buf[7 + payloadLen] | (buf[7 + payloadLen + 1] << 8);
  if (expected !== received) return null;
  return { payload: buf.subarray(7, 7 + payloadLen), streamId: buf[4] };
}

// ─── Protobuf decoder (zigzag sint32 + uint32) ─────────────────────────────

function decodeVarint(buf, off) {
  let val = 0, shift = 0;
  while (off < buf.length) {
    const b = buf[off++];
    val |= (b & 0x7F) << shift;
    if (!(b & 0x80)) return [val >>> 0, off];
    shift += 7;
  }
  return [val >>> 0, off];
}

function zigzagDecode(n) { return (n >>> 1) ^ -(n & 1); }

function decodeSensorProtobuf(buf) {
  const result = { temp: 0, ax: 0, ay: 0, az: 0, als: 0, ps: 0 };
  let off = 0;
  while (off < buf.length) {
    const [tag, o1] = decodeVarint(buf, off); off = o1;
    const fieldNum = tag >>> 3;
    const [val, o2] = decodeVarint(buf, off); off = o2;
    switch (fieldNum) {
      case 1: result.temp = zigzagDecode(val) / 10; break;
      case 2: result.ax = zigzagDecode(val); break;
      case 3: result.ay = zigzagDecode(val); break;
      case 4: result.az = zigzagDecode(val); break;
      case 5: result.als = val; break;
      case 6: result.ps = val; break;
    }
  }
  return result;
}

// ─── FrameAssembler ─────────────────────────────────────────────────────────

class FrameAssembler {
  constructor() { this.buf = Buffer.alloc(128); this.reset(); }
  reset() { this.state = 0; this.pos = 0; this.remaining = 0; }
  feedByte(b) {
    switch (this.state) {
      case 0: if (b===0xAC){this.buf[0]=b;this.pos=1;this.state=1;} break;
      case 1: if (b===0xDA){this.buf[this.pos++]=b;this.state=2;this.headerLeft=5;}
              else if(b===0xAC){this.pos=1;} else this.reset(); break;
      case 2: this.buf[this.pos++]=b; if(--this.headerLeft===0){
                const pl=this.buf[5]|(this.buf[6]<<8); this.remaining=pl+2;
                if(this.pos+this.remaining>128){this.reset();}else this.state=3;} break;
      case 3: this.buf[this.pos++]=b; if(--this.remaining===0){
                const r=Buffer.from(this.buf.subarray(0,this.pos)); this.reset(); return r;} break;
    }
    return null;
  }
}

// ─── Main ───────────────────────────────────────────────────────────────────

const HC08_SERVICE = 'ffe0';
const HC08_CHAR = 'ffe1';
const TARGET_NAME = 'ArcanaBLE';

async function main() {
  // Resolve decryption key
  let key;
  if (KEY_HEX) {
    key = Buffer.from(KEY_HEX, 'hex');
  } else if (UID_HEX) {
    const uid = Buffer.from(UID_HEX, 'hex');
    key = deriveDeviceKey(LEGACY_FLEET_MASTER, uid);
  } else {
    console.error('Usage: node ble-sensor-monitor.js --uid <24-hex> | --key <64-hex>');
    process.exit(1);
  }

  if (!JSON_MODE) {
    console.log('');
    console.log('  Arcana BLE Sensor Monitor');
    console.log(`  Key: ${key.subarray(0,8).toString('hex')}...`);
    console.log('');
  }

  // Wait for BT
  await new Promise((resolve, reject) => {
    if (noble.state === 'poweredOn') return resolve();
    noble.once('stateChange', (s) => s === 'poweredOn' ? resolve() : reject(new Error(s)));
  });

  // Scan
  if (!JSON_MODE) console.log('  Scanning...');
  const peripheral = await new Promise((resolve) => {
    const timer = setTimeout(() => { noble.stopScanning(); resolve(null); }, 15000);
    noble.on('discover', (p) => {
      if (p.advertisement.localName === TARGET_NAME) {
        clearTimeout(timer); noble.stopScanning(); resolve(p);
      }
    });
    noble.startScanning([], false);
  });
  if (!peripheral) { console.error('  ArcanaBLE not found'); process.exit(1); }

  // Connect
  if (!JSON_MODE) console.log(`  Connecting to ${peripheral.advertisement.localName} (RSSI:${peripheral.rssi})...`);
  await new Promise((res, rej) => peripheral.connect((e) => e ? rej(e) : res()));

  // Discover FFE0/FFE1
  const [, chars] = await new Promise((res, rej) =>
    peripheral.discoverSomeServicesAndCharacteristics([HC08_SERVICE], [HC08_CHAR],
      (e, s, c) => e ? rej(e) : res([s, c])));
  const uart = chars.find(c => c.uuid === HC08_CHAR);
  if (!uart) { console.error('  FFE1 not found'); process.exit(1); }

  if (!JSON_MODE) console.log('  Connected — listening for sensor frames (Ctrl+C to stop)\n');

  // Subscribe and decode
  const assembler = new FrameAssembler();
  let count = 0;

  uart.on('data', (data) => {
    for (const byte of data) {
      const frameBuf = assembler.feedByte(byte);
      if (!frameBuf) continue;

      const parsed = deframe(frameBuf);
      if (!parsed) continue;

      // Only process sensor stream (streamId 0x20)
      if (parsed.streamId !== 0x20) {
        if (RAW_MODE && !JSON_MODE) {
          console.log(`  [sid=${parsed.streamId.toString(16)}] ${parsed.payload.toString('hex')}`);
        }
        continue;
      }

      const payload = parsed.payload;
      if (payload.length < 13) continue; // at least nonce(12) + 1 byte

      // Decrypt: [nonce:12][encrypted_pb:N]
      const nonce = payload.subarray(0, 12);
      const encPb = Buffer.from(payload.subarray(12)); // copy for in-place decrypt
      const pb = chacha20Decrypt(key, nonce, encPb);

      // Decode protobuf
      const sensor = decodeSensorProtobuf(pb);
      count++;

      const tick = nonce.readUInt32LE(0);
      const ts = new Date().toISOString().substring(11, 23);

      if (JSON_MODE) {
        console.log(JSON.stringify({ ts, tick, ...sensor }));
      } else {
        if (RAW_MODE) {
          const hex = [...frameBuf].map(b => b.toString(16).padStart(2,'0')).join(' ');
          console.log(`  [${hex}]`);
        }
        console.log(
          `  #${String(count).padStart(4)} [${ts}] ` +
          `T=${sensor.temp.toFixed(1)}C ` +
          `ax=${String(sensor.ax).padStart(5)} ay=${String(sensor.ay).padStart(5)} az=${String(sensor.az).padStart(5)} ` +
          `ALS=${sensor.als} PS=${sensor.ps}  ` +
          `tick=${tick}`
        );
      }
    }
  });

  await new Promise((res, rej) => uart.subscribe((e) => e ? rej(e) : res()));

  // Keep running until Ctrl+C
  process.on('SIGINT', () => {
    console.log(`\n  ${count} frames received. Bye.`);
    peripheral.disconnect(() => process.exit(0));
  });
}

main().catch(e => { console.error('Fatal:', e.message); process.exit(1); });
