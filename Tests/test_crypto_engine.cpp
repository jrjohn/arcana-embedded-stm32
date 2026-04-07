/**
 * @file test_crypto_engine.cpp
 * @brief Tests for arcana::CryptoEngine (AES-256-CCM envelope)
 *
 * Covers init, encrypt/decrypt round-trip, replay protection, hex parsing,
 * boundary errors. Uses real mbedtls_ccm + mbedtls_sha256 compiled into the
 * test binary so the cipher behaviour matches production exactly.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>
#include <vector>

#include "CryptoEngine.hpp"

using arcana::CryptoEngine;

namespace {

void fillKey(uint8_t k[32], uint8_t base = 0xA0) {
    for (int i = 0; i < 32; ++i) k[i] = static_cast<uint8_t>(base + i);
}

} // namespace

// ── Init / round-trip ────────────────────────────────────────────────────────

TEST(CryptoEngineTest, InitAndRoundTrip) {
    uint8_t key[32]; fillKey(key);
    CryptoEngine eng;
    ASSERT_TRUE(eng.init(key));

    const char* msg = "hello arcana ccm";
    const size_t msgLen = std::strlen(msg);

    uint8_t buf[64];
    size_t outLen = 0;
    ASSERT_TRUE(eng.encrypt(reinterpret_cast<const uint8_t*>(msg),
                            msgLen, buf, sizeof(buf), outLen));
    EXPECT_EQ(outLen, msgLen + CryptoEngine::kOverhead);

    // Decrypt with a separate engine instance using the same key
    CryptoEngine dec;
    ASSERT_TRUE(dec.init(key));
    uint8_t plain[64];
    size_t plainLen = 0;
    ASSERT_TRUE(dec.decrypt(buf, outLen, plain, sizeof(plain), plainLen));
    EXPECT_EQ(plainLen, msgLen);
    EXPECT_EQ(0, std::memcmp(plain, msg, msgLen));
}

// ── Wrong key fails AEAD tag check ───────────────────────────────────────────

TEST(CryptoEngineTest, WrongKeyRejected) {
    uint8_t k1[32]; fillKey(k1, 0xA0);
    uint8_t k2[32]; fillKey(k2, 0xB0);
    CryptoEngine a, b;
    ASSERT_TRUE(a.init(k1));
    ASSERT_TRUE(b.init(k2));

    const char* msg = "secret payload";
    uint8_t buf[64], plain[64];
    size_t outLen = 0, plainLen = 0;
    ASSERT_TRUE(a.encrypt(reinterpret_cast<const uint8_t*>(msg),
                          std::strlen(msg), buf, sizeof(buf), outLen));
    EXPECT_FALSE(b.decrypt(buf, outLen, plain, sizeof(plain), plainLen));
}

// ── Tag corruption ──────────────────────────────────────────────────────────

TEST(CryptoEngineTest, TagCorruptionDetected) {
    uint8_t key[32]; fillKey(key);
    CryptoEngine a, b;
    ASSERT_TRUE(a.init(key));
    ASSERT_TRUE(b.init(key));

    uint8_t buf[64], plain[64];
    size_t outLen = 0, plainLen = 0;
    const char* msg = "auth me";
    ASSERT_TRUE(a.encrypt(reinterpret_cast<const uint8_t*>(msg),
                          std::strlen(msg), buf, sizeof(buf), outLen));

    buf[outLen - 1] ^= 0xFF;  // flip last byte of tag
    EXPECT_FALSE(b.decrypt(buf, outLen, plain, sizeof(plain), plainLen));
}

// ── Replay protection ───────────────────────────────────────────────────────

TEST(CryptoEngineTest, ReplayProtection) {
    uint8_t key[32]; fillKey(key);
    CryptoEngine tx, rx;
    ASSERT_TRUE(tx.init(key));
    ASSERT_TRUE(rx.init(key));

    uint8_t e1[64], e2[64], plain[64];
    size_t l1 = 0, l2 = 0, pl = 0;
    const char* m = "msg";

    ASSERT_TRUE(tx.encrypt(reinterpret_cast<const uint8_t*>(m), 3, e1, sizeof(e1), l1));
    ASSERT_TRUE(tx.encrypt(reinterpret_cast<const uint8_t*>(m), 3, e2, sizeof(e2), l2));

    // Decrypt second message first → bumps watermark to counter=1
    ASSERT_TRUE(rx.decrypt(e2, l2, plain, sizeof(plain), pl));
    // Replaying first message (counter=0) must be rejected
    EXPECT_FALSE(rx.decrypt(e1, l1, plain, sizeof(plain), pl));
    // Re-playing second message (counter=1) is also rejected — watermark is strict
    EXPECT_FALSE(rx.decrypt(e2, l2, plain, sizeof(plain), pl));
}

// ── Output buffer too small ─────────────────────────────────────────────────

TEST(CryptoEngineTest, OutBufTooSmallFails) {
    uint8_t key[32]; fillKey(key);
    CryptoEngine eng;
    ASSERT_TRUE(eng.init(key));

    uint8_t buf[8];   // not enough for overhead+payload
    size_t outLen = 0;
    EXPECT_FALSE(eng.encrypt(reinterpret_cast<const uint8_t*>("hi"), 2,
                             buf, sizeof(buf), outLen));
}

// ── Decrypt with too-short input ────────────────────────────────────────────

TEST(CryptoEngineTest, ShortInputDecryptFails) {
    uint8_t key[32]; fillKey(key);
    CryptoEngine eng;
    ASSERT_TRUE(eng.init(key));

    uint8_t plain[16];
    size_t pl = 0;
    uint8_t tooShort[8] = {0};
    EXPECT_FALSE(eng.decrypt(tooShort, sizeof(tooShort), plain, sizeof(plain), pl));
}

// ── Decrypt without init ────────────────────────────────────────────────────

TEST(CryptoEngineTest, OperationsRequireInit) {
    CryptoEngine eng;
    uint8_t buf[64];
    size_t outLen = 0;
    EXPECT_FALSE(eng.encrypt(reinterpret_cast<const uint8_t*>("x"), 1,
                             buf, sizeof(buf), outLen));
    EXPECT_FALSE(eng.decrypt(buf, sizeof(buf), buf, sizeof(buf), outLen));
}

// ── hexToKey ────────────────────────────────────────────────────────────────

TEST(CryptoEngineTest, HexToKeyValid) {
    const char* hex =
        "00112233445566778899aabbccddeeff"
        "0123456789ABCDEF0123456789abcdef";
    uint8_t key[32];
    ASSERT_TRUE(CryptoEngine::hexToKey(hex, key));
    EXPECT_EQ(key[0],  0x00);
    EXPECT_EQ(key[15], 0xFF);
    EXPECT_EQ(key[16], 0x01);
    EXPECT_EQ(key[31], 0xEF);
}

TEST(CryptoEngineTest, HexToKeyRejectsInvalid) {
    uint8_t key[32];
    EXPECT_FALSE(CryptoEngine::hexToKey(nullptr, key));
    // Wrong length
    EXPECT_FALSE(CryptoEngine::hexToKey("deadbeef", key));
    // Bad hex char
    const char* bad =
        "ZZ112233445566778899aabbccddeeff"
        "0123456789ABCDEF0123456789abcdef";
    EXPECT_FALSE(CryptoEngine::hexToKey(bad, key));
}

// ── Re-init replaces previous state ─────────────────────────────────────────

TEST(CryptoEngineTest, ReinitWorks) {
    uint8_t k1[32]; fillKey(k1, 0xA0);
    uint8_t k2[32]; fillKey(k2, 0xC0);
    CryptoEngine eng;
    ASSERT_TRUE(eng.init(k1));
    ASSERT_TRUE(eng.init(k2));   // free + re-init must succeed

    uint8_t buf[64];
    size_t outLen = 0;
    EXPECT_TRUE(eng.encrypt(reinterpret_cast<const uint8_t*>("ok"), 2,
                            buf, sizeof(buf), outLen));
}
