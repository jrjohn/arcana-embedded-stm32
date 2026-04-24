/**
 * @file Sha256.hpp
 * @brief Header-only SHA-256 + HMAC-SHA256 + HKDF-SHA256
 *
 * Zero mbedtls dependency. All stack-based, no heap allocation.
 * Based on FIPS 180-4 / RFC 2104 / RFC 5869.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace arcana {
namespace crypto {

class Sha256 {
public:
    static constexpr size_t HASH_SIZE = 32;
    static constexpr size_t BLOCK_SIZE = 64;

    // ─── SHA-256 one-shot ───────────────────────────────────────────────

    static void hash(const uint8_t* data, size_t len, uint8_t out[HASH_SIZE]) {
        Context ctx;
        init(ctx);
        update(ctx, data, len);
        final(ctx, out);
    }

    // ─── HMAC-SHA256 ────────────────────────────────────────────────────

    static void hmac(const uint8_t* key, size_t keyLen,
                     const uint8_t* data, size_t dataLen,
                     uint8_t out[HASH_SIZE]) {
        uint8_t kBlock[BLOCK_SIZE] = {};

        // Key preparation
        if (keyLen > BLOCK_SIZE) {
            hash(key, keyLen, kBlock);
        } else {
            memcpy(kBlock, key, keyLen);
        }

        // Inner hash: SHA256((key ^ ipad) || data)
        uint8_t iPad[BLOCK_SIZE];
        for (size_t i = 0; i < BLOCK_SIZE; i++) iPad[i] = kBlock[i] ^ 0x36;

        Context inner;
        init(inner);
        update(inner, iPad, BLOCK_SIZE);
        update(inner, data, dataLen);
        uint8_t innerHash[HASH_SIZE];
        final(inner, innerHash);

        // Outer hash: SHA256((key ^ opad) || innerHash)
        uint8_t oPad[BLOCK_SIZE];
        for (size_t i = 0; i < BLOCK_SIZE; i++) oPad[i] = kBlock[i] ^ 0x5C;

        Context outer;
        init(outer);
        update(outer, oPad, BLOCK_SIZE);
        update(outer, innerHash, HASH_SIZE);
        final(outer, out);
    }

    // ─── HKDF-SHA256 (single block, max 32 bytes output) ───────────────

    static bool hkdf(const uint8_t* ikm, size_t ikmLen,
                     const uint8_t* salt, size_t saltLen,
                     const uint8_t* info, size_t infoLen,
                     uint8_t* okm, size_t okmLen) {
        if (okmLen > HASH_SIZE) return false;

        // Extract: PRK = HMAC-SHA256(salt, ikm)
        uint8_t prk[HASH_SIZE];
        hmac(salt, saltLen, ikm, ikmLen, prk);

        // Expand: T(1) = HMAC-SHA256(PRK, info || 0x01)
        uint8_t expandBuf[128 + 1];
        if (infoLen > 127) return false;
        memcpy(expandBuf, info, infoLen);
        expandBuf[infoLen] = 0x01;

        uint8_t t[HASH_SIZE];
        hmac(prk, HASH_SIZE, expandBuf, infoLen + 1, t);
        memcpy(okm, t, okmLen);
        return true;
    }

private:
    struct Context {
        uint32_t state[8];
        uint8_t  buffer[BLOCK_SIZE];
        uint64_t totalLen;
        size_t   bufLen;
    };

    static void init(Context& ctx) {
        ctx.state[0] = 0x6a09e667; ctx.state[1] = 0xbb67ae85;
        ctx.state[2] = 0x3c6ef372; ctx.state[3] = 0xa54ff53a;
        ctx.state[4] = 0x510e527f; ctx.state[5] = 0x9b05688c;
        ctx.state[6] = 0x1f83d9ab; ctx.state[7] = 0x5be0cd19;
        ctx.totalLen = 0;
        ctx.bufLen = 0;
    }

    static void update(Context& ctx, const uint8_t* data, size_t len) {
        ctx.totalLen += len;
        while (len > 0) {
            size_t space = BLOCK_SIZE - ctx.bufLen;
            size_t chunk = len < space ? len : space;
            memcpy(ctx.buffer + ctx.bufLen, data, chunk);
            ctx.bufLen += chunk;
            data += chunk;
            len -= chunk;
            if (ctx.bufLen == BLOCK_SIZE) {
                transform(ctx.state, ctx.buffer);
                ctx.bufLen = 0;
            }
        }
    }

    static void final(Context& ctx, uint8_t out[HASH_SIZE]) {
        // Padding
        ctx.buffer[ctx.bufLen++] = 0x80;
        if (ctx.bufLen > 56) {
            memset(ctx.buffer + ctx.bufLen, 0, BLOCK_SIZE - ctx.bufLen);
            transform(ctx.state, ctx.buffer);
            ctx.bufLen = 0;
        }
        memset(ctx.buffer + ctx.bufLen, 0, 56 - ctx.bufLen);

        // Length in bits (big-endian)
        uint64_t bits = ctx.totalLen * 8;
        for (int i = 7; i >= 0; i--) {
            ctx.buffer[56 + (7 - i)] = (uint8_t)(bits >> (i * 8));
        }
        transform(ctx.state, ctx.buffer);

        // Output (big-endian)
        for (int i = 0; i < 8; i++) {
            out[i * 4 + 0] = (uint8_t)(ctx.state[i] >> 24);
            out[i * 4 + 1] = (uint8_t)(ctx.state[i] >> 16);
            out[i * 4 + 2] = (uint8_t)(ctx.state[i] >> 8);
            out[i * 4 + 3] = (uint8_t)(ctx.state[i]);
        }
    }

    // ─── SHA-256 block transform ────────────────────────────────────────

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    static uint32_t ch(uint32_t x, uint32_t y, uint32_t z)  { return (x & y) ^ (~x & z); }
    static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    static uint32_t ep0(uint32_t x)  { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
    static uint32_t ep1(uint32_t x)  { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
    static uint32_t sig0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
    static uint32_t sig1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

    static void transform(uint32_t state[8], const uint8_t block[BLOCK_SIZE]) {
        static constexpr uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
        };

        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
                   ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
        }
        for (int i = 16; i < 64; i++) {
            w[i] = sig1(w[i-2]) + w[i-7] + sig0(w[i-15]) + w[i-16];
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 64; i++) {
            uint32_t t1 = h + ep1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = ep0(a) + maj(a, b, c);
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
};

} // namespace crypto
} // namespace arcana
