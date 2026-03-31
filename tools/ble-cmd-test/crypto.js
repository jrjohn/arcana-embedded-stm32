/**
 * @file crypto.js
 * @brief Arcana crypto stack — ChaCha20, AES-256-CCM CryptoEngine, ECDH P-256 KeyExchange
 *
 * Ported from STM32 C++ implementation:
 *   ChaCha20.hpp       — RFC 7539 stream cipher (key derivation)
 *   CryptoEngine.hpp   — AES-256-CCM with nonce prefix + counter
 *   KeyExchangeManager — ECDH P-256, HKDF-SHA256, HMAC-SHA256
 *   DeviceKey.hpp      — Per-device key = ChaCha20(fleet_master, uid, 0)
 */

import crypto from 'node:crypto';

// ─── ChaCha20 (RFC 7539 — for device key derivation) ───────────────────────

function rotl32(v, n) {
  return ((v << n) | (v >>> (32 - n))) >>> 0;
}

function quarterRound(s, a, b, c, d) {
  s[a] = (s[a] + s[b]) >>> 0; s[d] ^= s[a]; s[d] = rotl32(s[d], 16);
  s[c] = (s[c] + s[d]) >>> 0; s[b] ^= s[c]; s[b] = rotl32(s[b], 12);
  s[a] = (s[a] + s[b]) >>> 0; s[d] ^= s[a]; s[d] = rotl32(s[d], 8);
  s[c] = (s[c] + s[d]) >>> 0; s[b] ^= s[c]; s[b] = rotl32(s[b], 7);
}

function loadLE32(buf, offset) {
  return buf[offset] | (buf[offset + 1] << 8) |
         (buf[offset + 2] << 16) | (buf[offset + 3] << 24);
}

/**
 * ChaCha20 encrypt/decrypt in-place (symmetric).
 * @param {Buffer} key   32 bytes
 * @param {Buffer} nonce 12 bytes
 * @param {number} counter Initial block counter
 * @param {Buffer} data  Modified in-place
 */
export function chacha20Crypt(key, nonce, counter, data) {
  const state = new Uint32Array(16);
  const working = new Uint32Array(16);
  const keystream = Buffer.alloc(64);

  let offset = 0;
  while (offset < data.length) {
    // Init state: "expand 32-byte k"
    state[0]  = 0x61707865;
    state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;
    state[3]  = 0x6b206574;
    for (let i = 0; i < 8; i++) state[4 + i] = loadLE32(key, i * 4);
    state[12] = counter;
    for (let i = 0; i < 3; i++) state[13 + i] = loadLE32(nonce, i * 4);

    // Copy to working
    working.set(state);

    // 20 rounds (10 double-rounds)
    for (let i = 0; i < 10; i++) {
      quarterRound(working, 0, 4, 8, 12);
      quarterRound(working, 1, 5, 9, 13);
      quarterRound(working, 2, 6, 10, 14);
      quarterRound(working, 3, 7, 11, 15);
      quarterRound(working, 0, 5, 10, 15);
      quarterRound(working, 1, 6, 11, 12);
      quarterRound(working, 2, 7, 8, 13);
      quarterRound(working, 3, 4, 9, 14);
    }

    // Add original state + serialize LE
    for (let i = 0; i < 16; i++) {
      const v = (working[i] + state[i]) >>> 0;
      keystream[i * 4 + 0] = v & 0xFF;
      keystream[i * 4 + 1] = (v >>> 8) & 0xFF;
      keystream[i * 4 + 2] = (v >>> 16) & 0xFF;
      keystream[i * 4 + 3] = (v >>> 24) & 0xFF;
    }

    // XOR with data
    const blockLen = Math.min(64, data.length - offset);
    for (let i = 0; i < blockLen; i++) {
      data[offset + i] ^= keystream[i];
    }

    counter++;
    offset += blockLen;
  }
}

// ─── Device Key Derivation ──────────────────────────────────────────────────

/** Legacy fleet master for unprovisioned dev boards */
export const LEGACY_FLEET_MASTER = Buffer.from([
  0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
  0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
  0xA5, 0x5A, 0x0F, 0xF0, 0x12, 0x34, 0x56, 0x78,
  0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22, 0x33, 0x44,
]);

/**
 * Derive per-device PSK from fleet master + hardware UID.
 * PSK = ChaCha20_keystream(fleetMaster, uid, counter=0)[:32]
 *
 * @param {Buffer} fleetMaster 32-byte fleet master key
 * @param {Buffer} uid         12-byte STM32 hardware UID
 * @returns {Buffer} 32-byte device PSK
 */
export function deriveDeviceKey(fleetMaster, uid) {
  const deviceKey = Buffer.alloc(32); // zeros
  chacha20Crypt(fleetMaster, uid, 0, deviceKey);
  return deviceKey;
}

// ─── CryptoEngine (AES-256-CCM) ────────────────────────────────────────────

export const KEY_LEN = 32;
export const TAG_LEN = 8;
export const COUNTER_LEN = 4;
export const CRYPTO_OVERHEAD = COUNTER_LEN + TAG_LEN; // 12
const NONCE_PREFIX_LEN = 9;

export class CryptoEngine {
  /**
   * @param {Buffer|string} key 32-byte key (or 64-char hex string)
   */
  constructor(key) {
    if (typeof key === 'string') key = Buffer.from(key, 'hex');
    if (key.length !== KEY_LEN) throw new Error(`Key must be ${KEY_LEN} bytes`);

    this.key = key;

    // Nonce prefix = SHA256(key || "ARCANA")[0:9]
    const h = crypto.createHash('sha256');
    h.update(key);
    h.update(Buffer.from('ARCANA'));
    this.noncePrefix = h.digest().subarray(0, NONCE_PREFIX_LEN);

    this.txCounter = 0;
    this.rxCounter = -1; // uninitialized
  }

  _buildNonce(counter) {
    const nonce = Buffer.alloc(13);
    this.noncePrefix.copy(nonce, 0);
    nonce.writeUInt32LE(counter, NONCE_PREFIX_LEN);
    return nonce;
  }

  /**
   * Encrypt plaintext. Returns [counter:4 LE][ciphertext:N][tag:8].
   * @param {Buffer} plaintext
   * @returns {Buffer}
   */
  encrypt(plaintext) {
    if (this.txCounter >= 0xFFFFFFFF) throw new Error('Nonce exhaustion');

    const counter = this.txCounter++;
    const nonce = this._buildNonce(counter);

    const cipher = crypto.createCipheriv('aes-256-ccm', this.key, nonce, {
      authTagLength: TAG_LEN,
    });

    const ciphertext = Buffer.concat([
      cipher.update(plaintext),
      cipher.final(),
    ]);
    const tag = cipher.getAuthTag();

    // Envelope: [counter:4 LE][ciphertext][tag:8]
    const out = Buffer.alloc(COUNTER_LEN + ciphertext.length + TAG_LEN);
    out.writeUInt32LE(counter, 0);
    ciphertext.copy(out, COUNTER_LEN);
    tag.copy(out, COUNTER_LEN + ciphertext.length);
    return out;
  }

  /**
   * Decrypt envelope [counter:4 LE][ciphertext:N][tag:8].
   * @param {Buffer} data
   * @returns {Buffer} plaintext
   */
  decrypt(data) {
    if (data.length < CRYPTO_OVERHEAD) throw new Error('Too short');

    const counter = data.readUInt32LE(0);

    // Replay protection
    if (this.rxCounter >= 0 && counter <= this.rxCounter) {
      throw new Error(`Replay: ${counter} <= ${this.rxCounter}`);
    }

    const nonce = this._buildNonce(counter);
    const ciphertextLen = data.length - COUNTER_LEN - TAG_LEN;
    const ciphertext = data.subarray(COUNTER_LEN, COUNTER_LEN + ciphertextLen);
    const tag = data.subarray(COUNTER_LEN + ciphertextLen);

    const decipher = crypto.createDecipheriv('aes-256-ccm', this.key, nonce, {
      authTagLength: TAG_LEN,
    });
    decipher.setAuthTag(tag);

    const plaintext = Buffer.concat([
      decipher.update(ciphertext),
      decipher.final(),
    ]);

    this.rxCounter = counter;
    return plaintext;
  }
}

// ─── HKDF-SHA256 (single-block, max 32 bytes output) ───────────────────────

function hmacSha256(key, data) {
  return crypto.createHmac('sha256', key).update(data).digest();
}

/**
 * HKDF-SHA256 extract + expand (single block).
 * @param {Buffer} ikm   Input keying material
 * @param {Buffer} salt
 * @param {Buffer} info
 * @param {number} length Output length (max 32)
 * @returns {Buffer}
 */
export function hkdfSha256(ikm, salt, info, length = 32) {
  // Extract: PRK = HMAC-SHA256(salt, ikm)
  const prk = hmacSha256(salt, ikm);
  // Expand: T(1) = HMAC-SHA256(PRK, info || 0x01)
  const expandInput = Buffer.concat([info, Buffer.from([0x01])]);
  const t = hmacSha256(prk, expandInput);
  return t.subarray(0, length);
}

// ─── ECDH P-256 Key Exchange (client side) ──────────────────────────────────

export class KeyExchangeClient {
  /**
   * @param {CryptoEngine} pskEngine CryptoEngine initialized with device PSK
   * @param {Buffer} psk Raw 32-byte PSK
   */
  constructor(pskEngine, psk) {
    this.pskEngine = pskEngine;
    this.psk = psk;
    this.sessionEngine = null;

    // Generate ephemeral P-256 keypair
    this.ecdh = crypto.createECDH('prime256v1');
    this.ecdh.generateKeys();
  }

  /**
   * Get client public key as 64 bytes (x || y, big-endian).
   * Node.js ECDH returns 65 bytes (0x04 || x || y), strip the prefix.
   */
  getClientPubKey() {
    const uncompressed = this.ecdh.getPublicKey();
    return uncompressed.subarray(1); // strip 0x04 prefix → 64 bytes
  }

  /**
   * Process KeyExchange response from server.
   * @param {Buffer} payload 96 bytes = serverPub(64) + authTag(32)
   * @returns {{ sessionEngine: CryptoEngine } | null}
   */
  processResponse(payload) {
    if (payload.length !== 96) {
      console.error(`[KE] Bad payload size: ${payload.length}`);
      return null;
    }

    const serverPub = payload.subarray(0, 64);
    const authTag = payload.subarray(64, 96);
    const clientPub = this.getClientPubKey();

    // Verify auth tag: HMAC-SHA256(PSK, serverPub || clientPub)
    const authData = Buffer.concat([serverPub, clientPub]);
    const expectedTag = hmacSha256(this.psk, authData);
    if (!crypto.timingSafeEqual(authTag, expectedTag)) {
      console.error('[KE] Auth tag MISMATCH — possible MITM!');
      return null;
    }
    console.log('  [KE] Auth tag verified OK');

    // Compute ECDH shared secret
    // Server pub needs 0x04 prefix for Node.js
    const serverPubFull = Buffer.concat([Buffer.from([0x04]), serverPub]);
    const sharedSecret = this.ecdh.computeSecret(serverPubFull);

    // Derive session key: HKDF-SHA256(sharedSecret, PSK, "ARCANA-SESSION", 32)
    const sessionKey = hkdfSha256(sharedSecret, this.psk, Buffer.from('ARCANA-SESSION'));

    console.log(`  [KE] Session key: ${sessionKey.subarray(0, 8).toString('hex')}...`);

    // Create session CryptoEngine
    this.sessionEngine = new CryptoEngine(sessionKey);
    return { sessionEngine: this.sessionEngine };
  }
}
