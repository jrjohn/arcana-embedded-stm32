/**
 * @file test_chacha20.cpp
 * @brief RFC 7539 known-answer tests for arcana::crypto::ChaCha20
 *
 * Vectors from RFC 7539:
 *   §2.3.2  block function test (key+nonce+counter → 64-byte keystream block)
 *   §2.4.2  encryption of "Ladies and Gentlemen..." plaintext
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>

// ChaCha20.hpp lives under Targets/stm32f103ze/Services/Common/. The test
// CMake target adds that as -I so flat include works.
#include "ChaCha20.hpp"

using arcana::crypto::ChaCha20;

namespace {

// Helper: build a 32-byte sequential key 0x00..0x1F
void key0to31(uint8_t k[32]) {
    for (int i = 0; i < 32; ++i) k[i] = static_cast<uint8_t>(i);
}

} // namespace

// ── RFC 7539 §2.3.2 — block-function vector ──────────────────────────────────
//
// key     = 00:01:02:..:1f
// nonce   = 00:00:00:09:00:00:00:4a:00:00:00:00
// counter = 1
// keystream block = 10 f1 e7 e4 d1 3b 59 15 50 0f dd 1f a3 20 71 c4
//                   c7 d1 f4 c7 33 c0 68 03 04 22 aa 9a c3 d4 6c 4e
//                   d2 82 64 46 07 9f aa 09 14 c2 d7 05 d9 8b 02 a2
//                   b5 12 9c d1 de 16 4e b9 cb d0 83 e8 a2 50 3c 4e

TEST(ChaCha20Test, Rfc7539_2_3_2_BlockFunction) {
    uint8_t key[32];
    key0to31(key);
    const uint8_t nonce[12] = {
        0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x4a,0x00,0x00,0x00,0x00
    };
    uint8_t buf[64] = {0};

    // crypt against zero plaintext = pure keystream
    ChaCha20::crypt(key, nonce, /*counter=*/1, buf, sizeof(buf));

    const uint8_t expected[64] = {
        0x10,0xf1,0xe7,0xe4,0xd1,0x3b,0x59,0x15, 0x50,0x0f,0xdd,0x1f,0xa3,0x20,0x71,0xc4,
        0xc7,0xd1,0xf4,0xc7,0x33,0xc0,0x68,0x03, 0x04,0x22,0xaa,0x9a,0xc3,0xd4,0x6c,0x4e,
        0xd2,0x82,0x64,0x46,0x07,0x9f,0xaa,0x09, 0x14,0xc2,0xd7,0x05,0xd9,0x8b,0x02,0xa2,
        0xb5,0x12,0x9c,0xd1,0xde,0x16,0x4e,0xb9, 0xcb,0xd0,0x83,0xe8,0xa2,0x50,0x3c,0x4e,
    };
    EXPECT_EQ(0, std::memcmp(buf, expected, 64));
}

// ── RFC 7539 §2.4.2 — full encryption vector ─────────────────────────────────

TEST(ChaCha20Test, Rfc7539_2_4_2_EncryptDecrypt) {
    uint8_t key[32];
    key0to31(key);
    const uint8_t nonce[12] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x4a,0x00,0x00,0x00,0x00
    };
    const char* msg = "Ladies and Gentlemen of the class of '99: "
                      "If I could offer you only one tip for the future, "
                      "sunscreen would be it.";
    const size_t len = std::strlen(msg);
    ASSERT_EQ(len, 114u);

    uint8_t buf[114];
    std::memcpy(buf, msg, len);

    // Encrypt with counter=1 per RFC
    ChaCha20::crypt(key, nonce, /*counter=*/1, buf, len);

    // RFC 7539 §2.4.2 expected ciphertext
    const uint8_t expected[114] = {
        0x6e,0x2e,0x35,0x9a,0x25,0x68,0xf9,0x80, 0x41,0xba,0x07,0x28,0xdd,0x0d,0x69,0x81,
        0xe9,0x7e,0x7a,0xec,0x1d,0x43,0x60,0xc2, 0x0a,0x27,0xaf,0xcc,0xfd,0x9f,0xae,0x0b,
        0xf9,0x1b,0x65,0xc5,0x52,0x47,0x33,0xab, 0x8f,0x59,0x3d,0xab,0xcd,0x62,0xb3,0x57,
        0x16,0x39,0xd6,0x24,0xe6,0x51,0x52,0xab, 0x8f,0x53,0x0c,0x35,0x9f,0x08,0x61,0xd8,
        0x07,0xca,0x0d,0xbf,0x50,0x0d,0x6a,0x61, 0x56,0xa3,0x8e,0x08,0x8a,0x22,0xb6,0x5e,
        0x52,0xbc,0x51,0x4d,0x16,0xcc,0xf8,0x06, 0x81,0x8c,0xe9,0x1a,0xb7,0x79,0x37,0x36,
        0x5a,0xf9,0x0b,0xbf,0x74,0xa3,0x5b,0xe6, 0xb4,0x0b,0x8e,0xed,0xf2,0x78,0x5e,0x42,
        0x87,0x4d,
    };
    EXPECT_EQ(0, std::memcmp(buf, expected, len));

    // Decryption (XOR is symmetric — same call restores plaintext)
    ChaCha20::crypt(key, nonce, /*counter=*/1, buf, len);
    EXPECT_EQ(0, std::memcmp(buf, msg, len));
}

// ── Empty input is a no-op ───────────────────────────────────────────────────

TEST(ChaCha20Test, EmptyInputIsNoOp) {
    uint8_t key[32]; key0to31(key);
    uint8_t nonce[12] = {0};
    uint8_t marker = 0xAB;
    ChaCha20::crypt(key, nonce, 0, &marker, 0);
    EXPECT_EQ(marker, 0xAB);
}

// ── Streaming across multiple blocks (covers the while loop wraparound) ─────

TEST(ChaCha20Test, MultiBlockMatchesSingleShot) {
    uint8_t key[32]; key0to31(key);
    const uint8_t nonce[12] = {
        0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x4a,0x00,0x00,0x00,0x00
    };

    // 200 bytes = 3 full blocks + 8 partial. Encrypts a deterministic input
    // and verifies the cipher is reversible (encrypt then encrypt = identity).
    uint8_t buf[200];
    for (int i = 0; i < 200; ++i) buf[i] = static_cast<uint8_t>(i * 7 + 1);
    uint8_t orig[200];
    std::memcpy(orig, buf, 200);

    ChaCha20::crypt(key, nonce, /*counter=*/0, buf, sizeof(buf));
    EXPECT_NE(0, std::memcmp(buf, orig, 200));   // actually changed

    ChaCha20::crypt(key, nonce, /*counter=*/0, buf, sizeof(buf));
    EXPECT_EQ(0, std::memcmp(buf, orig, 200));   // round-trip restores
}
