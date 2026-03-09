#pragma once

#include <stdint.h>
#include <string.h>

namespace arcana {
namespace crypto {

/**
 * ChaCha20 stream cipher (RFC 7539).
 * Lightweight software implementation suitable for embedded systems.
 * No lookup tables, constant-time operations.
 */
class ChaCha20 {
public:
    static const uint8_t KEY_SIZE   = 32;  // 256-bit key
    static const uint8_t NONCE_SIZE = 12;  // 96-bit nonce
    static const uint8_t BLOCK_SIZE = 64;  // 512-bit block

    /**
     * Encrypt or decrypt data in-place.
     * ChaCha20 is symmetric: encrypt == decrypt.
     *
     * @param key     32-byte key
     * @param nonce   12-byte nonce (must be unique per message)
     * @param counter Initial block counter (usually 0 or 1)
     * @param data    Data buffer (modified in-place)
     * @param len     Data length in bytes
     */
    static void crypt(const uint8_t key[KEY_SIZE],
                      const uint8_t nonce[NONCE_SIZE],
                      uint32_t counter,
                      uint8_t* data,
                      uint32_t len) {
        uint32_t state[16];
        uint8_t keystream[BLOCK_SIZE];

        uint32_t offset = 0;
        while (offset < len) {
            // Initialize state
            initState(state, key, nonce, counter);

            // 20 rounds (10 double-rounds)
            uint32_t working[16];
            memcpy(working, state, sizeof(state));

            for (int i = 0; i < 10; i++) {
                // Column rounds
                quarterRound(working, 0, 4,  8, 12);
                quarterRound(working, 1, 5,  9, 13);
                quarterRound(working, 2, 6, 10, 14);
                quarterRound(working, 3, 7, 11, 15);
                // Diagonal rounds
                quarterRound(working, 0, 5, 10, 15);
                quarterRound(working, 1, 6, 11, 12);
                quarterRound(working, 2, 7,  8, 13);
                quarterRound(working, 3, 4,  9, 14);
            }

            // Add original state
            for (int i = 0; i < 16; i++) {
                working[i] += state[i];
            }

            // Serialize to keystream (little-endian)
            for (int i = 0; i < 16; i++) {
                keystream[i * 4 + 0] = (working[i] >>  0) & 0xFF;
                keystream[i * 4 + 1] = (working[i] >>  8) & 0xFF;
                keystream[i * 4 + 2] = (working[i] >> 16) & 0xFF;
                keystream[i * 4 + 3] = (working[i] >> 24) & 0xFF;
            }

            // XOR with data
            uint32_t blockLen = len - offset;
            if (blockLen > BLOCK_SIZE) blockLen = BLOCK_SIZE;

            for (uint32_t i = 0; i < blockLen; i++) {
                data[offset + i] ^= keystream[i];
            }

            counter++;
            offset += blockLen;
        }
    }

private:
    static uint32_t rotl32(uint32_t v, int n) {
        return (v << n) | (v >> (32 - n));
    }

    static void quarterRound(uint32_t* s, int a, int b, int c, int d) {
        s[a] += s[b]; s[d] ^= s[a]; s[d] = rotl32(s[d], 16);
        s[c] += s[d]; s[b] ^= s[c]; s[b] = rotl32(s[b], 12);
        s[a] += s[b]; s[d] ^= s[a]; s[d] = rotl32(s[d],  8);
        s[c] += s[d]; s[b] ^= s[c]; s[b] = rotl32(s[b],  7);
    }

    static uint32_t loadLE32(const uint8_t* p) {
        return (uint32_t)p[0]       | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    static void initState(uint32_t state[16],
                          const uint8_t key[KEY_SIZE],
                          const uint8_t nonce[NONCE_SIZE],
                          uint32_t counter) {
        // "expand 32-byte k" constant
        state[0]  = 0x61707865;
        state[1]  = 0x3320646e;
        state[2]  = 0x79622d32;
        state[3]  = 0x6b206574;
        // Key (8 words)
        state[4]  = loadLE32(key +  0);
        state[5]  = loadLE32(key +  4);
        state[6]  = loadLE32(key +  8);
        state[7]  = loadLE32(key + 12);
        state[8]  = loadLE32(key + 16);
        state[9]  = loadLE32(key + 20);
        state[10] = loadLE32(key + 24);
        state[11] = loadLE32(key + 28);
        // Counter
        state[12] = counter;
        // Nonce (3 words)
        state[13] = loadLE32(nonce + 0);
        state[14] = loadLE32(nonce + 4);
        state[15] = loadLE32(nonce + 8);
    }
};

} // namespace crypto
} // namespace arcana
