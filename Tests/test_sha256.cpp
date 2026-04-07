#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include "Sha256.hpp"

using arcana::crypto::Sha256;

namespace {

std::array<uint8_t, 32> hashOf(const void* data, size_t len) {
    std::array<uint8_t, 32> out{};
    Sha256::hash(static_cast<const uint8_t*>(data), len, out.data());
    return out;
}

bool eq(const std::array<uint8_t, 32>& a, const uint8_t (&b)[32]) {
    return std::memcmp(a.data(), b, 32) == 0;
}

} // namespace

// ── SHA-256 known vectors (FIPS 180-4 / NIST) ─────────────────────────────────

TEST(Sha256Test, EmptyVector) {
    // SHA256("") — exercises final() without the extra-block branch
    const uint8_t expected[32] = {
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14, 0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c, 0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55,
    };
    auto got = hashOf("", 0);
    EXPECT_TRUE(eq(got, expected));
}

TEST(Sha256Test, AbcVector) {
    // SHA256("abc") — single short block, basic transform path
    const uint8_t expected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea, 0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c, 0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad,
    };
    auto got = hashOf("abc", 3);
    EXPECT_TRUE(eq(got, expected));
}

TEST(Sha256Test, FiftySixByteVector) {
    // 56-byte NIST vector — after 0x80 padding, bufLen becomes 57 (>56),
    // forcing the extra-block branch in final()
    const char* msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    ASSERT_EQ(std::strlen(msg), 56u);
    const uint8_t expected[32] = {
        0x24,0x8d,0x6a,0x61,0xd2,0x06,0x38,0xb8, 0xe5,0xc0,0x26,0x93,0x0c,0x3e,0x60,0x39,
        0xa3,0x3c,0xe4,0x59,0x64,0xff,0x21,0x67, 0xf6,0xec,0xed,0xd4,0x19,0xdb,0x06,0xc1,
    };
    auto got = hashOf(msg, 56);
    EXPECT_TRUE(eq(got, expected));
}

TEST(Sha256Test, MultiBlockInput) {
    // 200-byte input forces multiple transforms inside update()
    // (covers the `bufLen == BLOCK_SIZE` branch).
    uint8_t buf[200];
    for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = static_cast<uint8_t>(i & 0xFF);

    auto first  = hashOf(buf, sizeof(buf));
    auto second = hashOf(buf, sizeof(buf));
    EXPECT_EQ(first, second);

    // SHA256 of bytes 0x00..0xC7 (verified via Python hashlib)
    const uint8_t expected[32] = {
        0x19,0x01,0xda,0x1c,0x9f,0x69,0x9b,0x48, 0xf6,0xb2,0x63,0x6e,0x65,0xcb,0xf7,0x3a,
        0xbf,0x99,0xd0,0x44,0x1e,0xf6,0x7f,0x5c, 0x54,0x0a,0x42,0xf7,0x05,0x1d,0xec,0x6f,
    };
    EXPECT_TRUE(eq(first, expected));
}

// ── HMAC-SHA256 (RFC 4231) ────────────────────────────────────────────────────

TEST(Sha256Test, HmacRfc4231Case1) {
    // key = 0x0b * 20, data = "Hi There"  → short key path (memcpy branch)
    uint8_t key[20];
    std::memset(key, 0x0b, sizeof(key));
    const char* data = "Hi There";

    uint8_t out[32];
    Sha256::hmac(key, sizeof(key),
                 reinterpret_cast<const uint8_t*>(data), 8, out);

    const uint8_t expected[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53, 0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7, 0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7,
    };
    EXPECT_EQ(0, std::memcmp(out, expected, 32));
}

TEST(Sha256Test, HmacRfc4231Case6_LongKey) {
    // key = 0xaa * 131 (>64) → exercises the `keyLen > BLOCK_SIZE` hash branch
    uint8_t key[131];
    std::memset(key, 0xaa, sizeof(key));
    const char* data = "Test Using Larger Than Block-Size Key - Hash Key First";

    uint8_t out[32];
    Sha256::hmac(key, sizeof(key),
                 reinterpret_cast<const uint8_t*>(data), std::strlen(data), out);

    const uint8_t expected[32] = {
        0x60,0xe4,0x31,0x59,0x1e,0xe0,0xb6,0x7f, 0x0d,0x8a,0x26,0xaa,0xcb,0xf5,0xb7,0x7f,
        0x8e,0x0b,0xc6,0x21,0x37,0x28,0xc5,0x14, 0x05,0x46,0x04,0x0f,0x0e,0xe3,0x7f,0x54,
    };
    EXPECT_EQ(0, std::memcmp(out, expected, 32));
}

// ── HKDF-SHA256 (RFC 5869) ────────────────────────────────────────────────────

TEST(Sha256Test, HkdfRfc5869Case1) {
    // IKM = 0x0b * 22, salt = 0x00..0x0c (13B), info = 0xf0..0xf9 (10B), L=32
    uint8_t ikm[22];
    std::memset(ikm, 0x0b, sizeof(ikm));
    const uint8_t salt[13] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07, 0x08,0x09,0x0a,0x0b,0x0c,
    };
    const uint8_t info[10] = {
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7, 0xf8,0xf9,
    };
    uint8_t okm[32] = {0};
    bool ok = Sha256::hkdf(ikm, sizeof(ikm),
                           salt, sizeof(salt),
                           info, sizeof(info),
                           okm, sizeof(okm));
    ASSERT_TRUE(ok);

    // First 32 bytes of OKM = T(1) from RFC 5869 Test Case 1
    const uint8_t expected[32] = {
        0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a, 0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a,
        0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c, 0x5d,0xb0,0x2d,0x56,0xec,0xc4,0xc5,0xbf,
    };
    EXPECT_EQ(0, std::memcmp(okm, expected, 32));
}

TEST(Sha256Test, HkdfRejectsOkmTooLarge) {
    uint8_t ikm[8] = {1,2,3,4,5,6,7,8};
    uint8_t okm[33];
    EXPECT_FALSE(Sha256::hkdf(ikm, sizeof(ikm),
                              nullptr, 0,
                              nullptr, 0,
                              okm, sizeof(okm)));
}

TEST(Sha256Test, HkdfRejectsInfoTooLarge) {
    uint8_t ikm[8] = {1,2,3,4,5,6,7,8};
    uint8_t info[128] = {0};   // > 127
    uint8_t okm[16];
    EXPECT_FALSE(Sha256::hkdf(ikm, sizeof(ikm),
                              nullptr, 0,
                              info, sizeof(info),
                              okm, sizeof(okm)));
}
