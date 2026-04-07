/**
 * @file test_key_exchange.cpp
 * @brief Tests for arcana::KeyExchangeManager
 *
 * Drives the full ECDH P-256 → HKDF → CCM session install path. Both ends
 * (client + server) run on host using real mbedtls so the test exercises the
 * production crypto code, not a mock.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>

#include "KeyExchangeManager.hpp"

#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

using arcana::KeyExchangeManager;
using arcana::CryptoEngine;

namespace {

/**
 * Generate an ECDH client keypair and return the raw public key (x||y, 64B).
 * Used by tests to drive performKeyExchange from the "outside".
 */
struct ClientKey {
    uint8_t pub[64];
    mbedtls_ecp_group grp;
    mbedtls_mpi d;          // private scalar
    mbedtls_ecp_point Q;    // public point
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;

    ClientKey() {
        mbedtls_ecp_group_init(&grp);
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_init(&Q);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);

        const char* p = "arcana_test";
        EXPECT_EQ(0, mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                           reinterpret_cast<const unsigned char*>(p),
                                           std::strlen(p)));
        EXPECT_EQ(0, mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1));
        EXPECT_EQ(0, mbedtls_ecp_gen_keypair(&grp, &d, &Q,
                                             mbedtls_ctr_drbg_random, &drbg));
        EXPECT_EQ(0, mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(X), pub,      32));
        EXPECT_EQ(0, mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(Y), pub + 32, 32));
    }

    ~ClientKey() {
        mbedtls_ecp_point_free(&Q);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_group_free(&grp);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
};

void fillPsk(uint8_t psk[32]) {
    for (int i = 0; i < 32; ++i) psk[i] = static_cast<uint8_t>(0xC0 + i);
}

} // namespace

// ── Init ─────────────────────────────────────────────────────────────────────

TEST(KeyExchangeTest, InitSucceeds) {
    KeyExchangeManager mgr;
    uint8_t psk[32]; fillPsk(psk);
    EXPECT_TRUE(mgr.init(psk));
}

// ── Full ECDH handshake → install → encrypt/decrypt ─────────────────────────

TEST(KeyExchangeTest, FullHandshakeAndSession) {
    KeyExchangeManager mgr;
    uint8_t psk[32]; fillPsk(psk);
    ASSERT_TRUE(mgr.init(psk));

    ClientKey client;

    uint8_t serverPub[64];
    uint8_t authTag[32];
    ASSERT_TRUE(mgr.performKeyExchange(/*source=*/1, /*connId=*/42,
                                       client.pub, serverPub, authTag));

    // Pending → not yet usable
    EXPECT_FALSE(mgr.hasSession(1, 42));

    // Install session
    ASSERT_TRUE(mgr.installPendingSession(1, 42));
    EXPECT_TRUE(mgr.hasSession(1, 42));

    // Round-trip with the installed session key
    const char* msg = "session works";
    uint8_t enc[64], dec[64];
    size_t encLen = 0, decLen = 0;
    ASSERT_TRUE(mgr.encryptWithSession(1, 42,
        reinterpret_cast<const uint8_t*>(msg), std::strlen(msg),
        enc, sizeof(enc), encLen));
    ASSERT_TRUE(mgr.decryptWithSession(1, 42, enc, encLen, dec, sizeof(dec), decLen));
    EXPECT_EQ(decLen, std::strlen(msg));
    EXPECT_EQ(0, std::memcmp(dec, msg, decLen));

    mgr.removeSession(1, 42);
    EXPECT_FALSE(mgr.hasSession(1, 42));
}

// ── Duplicate handshake on same (source, connId) is rejected ────────────────

TEST(KeyExchangeTest, DuplicateHandshakeRejected) {
    KeyExchangeManager mgr;
    uint8_t psk[32]; fillPsk(psk);
    ASSERT_TRUE(mgr.init(psk));

    ClientKey c1, c2;
    uint8_t serverPub[64], authTag[32];

    ASSERT_TRUE(mgr.performKeyExchange(2, 7, c1.pub, serverPub, authTag));
    // Second exchange while pending is still in place → rejected
    EXPECT_FALSE(mgr.performKeyExchange(2, 7, c2.pub, serverPub, authTag));
}

// ── Install without prior performKeyExchange fails ──────────────────────────

TEST(KeyExchangeTest, InstallWithoutHandshakeFails) {
    KeyExchangeManager mgr;
    uint8_t psk[32]; fillPsk(psk);
    ASSERT_TRUE(mgr.init(psk));
    EXPECT_FALSE(mgr.installPendingSession(1, 1));
}

// ── encrypt/decryptWithSession on missing session returns false ─────────────

TEST(KeyExchangeTest, EncryptWithoutSessionFails) {
    KeyExchangeManager mgr;
    uint8_t psk[32]; fillPsk(psk);
    ASSERT_TRUE(mgr.init(psk));

    uint8_t buf[64];
    size_t l = 0;
    EXPECT_FALSE(mgr.encryptWithSession(9, 9,
        reinterpret_cast<const uint8_t*>("x"), 1, buf, sizeof(buf), l));
    EXPECT_FALSE(mgr.decryptWithSession(9, 9, buf, sizeof(buf), buf, sizeof(buf), l));
}

// ── Reject invalid client public key (off-curve / zero point) ───────────────

TEST(KeyExchangeTest, RejectsInvalidClientPubkey) {
    KeyExchangeManager mgr;
    uint8_t psk[32]; fillPsk(psk);
    ASSERT_TRUE(mgr.init(psk));

    uint8_t badPub[64] = {0};   // (0,0) is not on the curve
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(mgr.performKeyExchange(1, 1, badPub, serverPub, authTag));
}

// ── Two distinct sessions in parallel slots ─────────────────────────────────

TEST(KeyExchangeTest, TwoSessionsInDistinctSlots) {
    KeyExchangeManager mgr;
    uint8_t psk[32]; fillPsk(psk);
    ASSERT_TRUE(mgr.init(psk));

    ClientKey c1, c2;
    uint8_t sPub[64], tag[32];

    // Session 1
    ASSERT_TRUE(mgr.performKeyExchange(1, 100, c1.pub, sPub, tag));
    ASSERT_TRUE(mgr.installPendingSession(1, 100));

    // Session 2 (different source)
    ASSERT_TRUE(mgr.performKeyExchange(2, 200, c2.pub, sPub, tag));
    ASSERT_TRUE(mgr.installPendingSession(2, 200));

    EXPECT_TRUE(mgr.hasSession(1, 100));
    EXPECT_TRUE(mgr.hasSession(2, 200));
    EXPECT_FALSE(mgr.hasSession(1, 999));

    mgr.removeSession(1, 100);
    EXPECT_FALSE(mgr.hasSession(1, 100));
    EXPECT_TRUE(mgr.hasSession(2, 200));
}
