#include "KeyExchangeManager.hpp"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/md.h"
#include <cstring>

namespace arcana {

KeyExchangeManager::KeyExchangeManager()
    : mSessions{}
    , mPending{}
    , mPsk{}
    , mMutex(nullptr)
    , mMutexBuf{}
{}

KeyExchangeManager::~KeyExchangeManager() {}

bool KeyExchangeManager::init(const uint8_t psk[CryptoEngine::kKeyLen]) {
    memcpy(mPsk, psk, CryptoEngine::kKeyLen);
    mMutex = xSemaphoreCreateMutexStatic(&mMutexBuf);
    return mMutex != nullptr;
}

// ---------------------------------------------------------------------------
// HMAC-SHA256 + HKDF-SHA256
// ---------------------------------------------------------------------------

bool KeyExchangeManager::hmacSha256(const uint8_t* key, size_t keyLen,
                                     const uint8_t* data, size_t dataLen,
                                     uint8_t out[32]) {
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!mdInfo) return false;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    int ret = mbedtls_md_setup(&ctx, mdInfo, 1); // 1 = HMAC
    if (ret != 0) { mbedtls_md_free(&ctx); return false; }

    ret = mbedtls_md_hmac_starts(&ctx, key, keyLen);
    if (ret == 0) ret = mbedtls_md_hmac_update(&ctx, data, dataLen);
    if (ret == 0) ret = mbedtls_md_hmac_finish(&ctx, out);

    mbedtls_md_free(&ctx);
    return ret == 0;
}

bool KeyExchangeManager::hkdfSha256(const uint8_t* ikm, size_t ikmLen,
                                     const uint8_t* salt, size_t saltLen,
                                     const uint8_t* info, size_t infoLen,
                                     uint8_t* okm, size_t okmLen) {
    // Extract: PRK = HMAC-SHA256(salt, ikm)
    uint8_t prk[32];
    if (!hmacSha256(salt, saltLen, ikm, ikmLen, prk)) return false;

    // Expand: T(1) = HMAC-SHA256(PRK, info || 0x01)
    if (okmLen > 32) return false;

    uint8_t expandInput[128];
    if (infoLen + 1 > sizeof(expandInput)) return false;

    memcpy(expandInput, info, infoLen);
    expandInput[infoLen] = 0x01;

    uint8_t t[32];
    if (!hmacSha256(prk, 32, expandInput, infoLen + 1, t)) return false;

    memcpy(okm, t, okmLen);
    return true;
}

// ---------------------------------------------------------------------------
// ECDH Key Exchange
// ---------------------------------------------------------------------------

bool KeyExchangeManager::performKeyExchange(uint8_t source, uint16_t connId,
                                             const uint8_t clientPub[64],
                                             uint8_t serverPub[64], uint8_t authTag[32]) {
    // Reject duplicate
    xSemaphoreTake(mMutex, portMAX_DELAY);
    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].active && mSessions[i].source == source &&
            mSessions[i].connId == connId) {
            xSemaphoreGive(mMutex);
            return false;
        }
    }
    if (mPending.valid && mPending.source == source && mPending.connId == connId) {
        xSemaphoreGive(mMutex);
        return false;
    }
    xSemaphoreGive(mMutex);

    // Init RNG
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctrDrbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctrDrbg);

    const char* pers = "arcana_ecdh";
    int ret = mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
                                     reinterpret_cast<const unsigned char*>(pers),
                                     strlen(pers));
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&ctrDrbg);
        mbedtls_entropy_free(&entropy);
        return false;
    }

    // Setup ECP
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point Q, Qp;
    mbedtls_mpi z;

    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);
    mbedtls_ecp_point_init(&Qp);
    mbedtls_mpi_init(&z);

    bool ok = false;
    do {
        ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
        if (ret != 0) break;

        // Generate server keypair
        ret = mbedtls_ecp_gen_keypair(&grp, &d, &Q,
                                       mbedtls_ctr_drbg_random, &ctrDrbg);
        if (ret != 0) break;

        // Load client public key (raw x||y, 32 bytes each, big-endian)
        ret = mbedtls_mpi_read_binary(&Qp.MBEDTLS_PRIVATE(X), clientPub, 32);
        if (ret != 0) break;
        ret = mbedtls_mpi_read_binary(&Qp.MBEDTLS_PRIVATE(Y), clientPub + 32, 32);
        if (ret != 0) break;
        ret = mbedtls_mpi_lset(&Qp.MBEDTLS_PRIVATE(Z), 1);
        if (ret != 0) break;

        // Validate client public key
        ret = mbedtls_ecp_check_pubkey(&grp, &Qp);
        if (ret != 0) break;

        // Compute shared secret
        ret = mbedtls_ecdh_compute_shared(&grp, &z, &Qp, &d,
                                           mbedtls_ctr_drbg_random, &ctrDrbg);
        if (ret != 0) break;

        // Export shared secret
        uint8_t sharedSecret[32];
        ret = mbedtls_mpi_write_binary(&z, sharedSecret, 32);
        if (ret != 0) break;

        // Export server public key (x||y)
        ret = mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(X), serverPub, 32);
        if (ret != 0) break;
        ret = mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(Y), serverPub + 32, 32);
        if (ret != 0) break;

        // Derive session key: HKDF(ikm=sharedSecret, salt=PSK, info="ARCANA-SESSION")
        const uint8_t info[] = "ARCANA-SESSION";
        uint8_t sessionKey[CryptoEngine::kKeyLen];
        if (!hkdfSha256(sharedSecret, 32, mPsk, sizeof(mPsk),
                         info, sizeof(info) - 1,
                         sessionKey, sizeof(sessionKey))) {
            break;
        }

        // Auth tag: HMAC-SHA256(PSK, serverPub || clientPub)
        uint8_t authData[128];
        memcpy(authData, serverPub, 64);
        memcpy(authData + 64, clientPub, 64);
        if (!hmacSha256(mPsk, sizeof(mPsk), authData, 128, authTag)) {
            break;
        }

        // Stage pending session
        xSemaphoreTake(mMutex, portMAX_DELAY);
        mPending.valid = true;
        mPending.source = source;
        mPending.connId = connId;
        memcpy(mPending.key, sessionKey, sizeof(sessionKey));
        xSemaphoreGive(mMutex);

        ok = true;
    } while (false);

    // Cleanup
    mbedtls_mpi_free(&z);
    mbedtls_ecp_point_free(&Qp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    mbedtls_ctr_drbg_free(&ctrDrbg);
    mbedtls_entropy_free(&entropy);

    return ok;
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

bool KeyExchangeManager::installPendingSession(uint8_t source, uint16_t connId) {
    xSemaphoreTake(mMutex, portMAX_DELAY);

    if (!mPending.valid || mPending.source != source || mPending.connId != connId) {
        xSemaphoreGive(mMutex);
        return false;
    }

    // Find existing or empty slot
    int slot = -1;
    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].active && mSessions[i].source == source &&
            mSessions[i].connId == connId) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < kMaxSessions; ++i) {
            if (!mSessions[i].active) { slot = i; break; }
        }
    }
    if (slot < 0) {
        xSemaphoreGive(mMutex);
        mPending.valid = false;
        return false;
    }

    // Init CryptoEngine with session key
    if (!mSessions[slot].engine.init(mPending.key)) {
        xSemaphoreGive(mMutex);
        mPending.valid = false;
        return false;
    }

    mSessions[slot].active = true;
    mSessions[slot].source = source;
    mSessions[slot].connId = connId;
    mPending.valid = false;

    xSemaphoreGive(mMutex);
    return true;
}

bool KeyExchangeManager::decryptWithSession(uint8_t source, uint16_t connId,
                                             const uint8_t* in, size_t inLen,
                                             uint8_t* plain, size_t plainBufSize,
                                             size_t& plainLen) {
    xSemaphoreTake(mMutex, portMAX_DELAY);
    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].active && mSessions[i].source == source &&
            mSessions[i].connId == connId) {
            bool ok = mSessions[i].engine.decrypt(in, inLen, plain, plainBufSize, plainLen);
            xSemaphoreGive(mMutex);
            return ok;
        }
    }
    xSemaphoreGive(mMutex);
    return false;
}

bool KeyExchangeManager::encryptWithSession(uint8_t source, uint16_t connId,
                                             const uint8_t* plain, size_t plainLen,
                                             uint8_t* out, size_t outBufSize,
                                             size_t& outLen) {
    xSemaphoreTake(mMutex, portMAX_DELAY);
    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].active && mSessions[i].source == source &&
            mSessions[i].connId == connId) {
            bool ok = mSessions[i].engine.encrypt(plain, plainLen, out, outBufSize, outLen);
            xSemaphoreGive(mMutex);
            return ok;
        }
    }
    xSemaphoreGive(mMutex);
    return false;
}

bool KeyExchangeManager::hasSession(uint8_t source, uint16_t connId) {
    xSemaphoreTake(mMutex, portMAX_DELAY);
    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].active && mSessions[i].source == source &&
            mSessions[i].connId == connId) {
            xSemaphoreGive(mMutex);
            return true;
        }
    }
    xSemaphoreGive(mMutex);
    return false;
}

void KeyExchangeManager::removeSession(uint8_t source, uint16_t connId) {
    xSemaphoreTake(mMutex, portMAX_DELAY);
    for (int i = 0; i < kMaxSessions; ++i) {
        if (mSessions[i].active && mSessions[i].source == source &&
            mSessions[i].connId == connId) {
            mSessions[i].active = false;
            break;
        }
    }
    if (mPending.valid && mPending.source == source && mPending.connId == connId) {
        mPending.valid = false;
    }
    xSemaphoreGive(mMutex);
}

} // namespace arcana
