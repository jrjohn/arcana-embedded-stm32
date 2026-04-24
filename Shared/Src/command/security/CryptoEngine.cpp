#include "CryptoEngine.hpp"
#include "mbedtls/sha256.h"
#include <cstring>

// mbedtls_ms_time stub for bare-metal (MBEDTLS_PLATFORM_MS_TIME_ALT)
extern "C" {
#include "mbedtls/platform_time.h"
mbedtls_ms_time_t mbedtls_ms_time(void) { return 0; }
}

namespace arcana {

CryptoEngine::~CryptoEngine() {
    if (mInitialized) {
        mbedtls_ccm_free(&mCtx);
    }
}

bool CryptoEngine::init(const uint8_t key[kKeyLen]) {
    if (mInitialized) {
        mbedtls_ccm_free(&mCtx);
        mInitialized = false;
    }
    mbedtls_ccm_init(&mCtx);

    int ret = mbedtls_ccm_setkey(&mCtx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret != 0) {
        mbedtls_ccm_free(&mCtx);
        return false;
    }

    // Derive nonce prefix: SHA256(key || "ARCANA")[0..8]
    uint8_t hash[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);          // 0 = SHA-256
    mbedtls_sha256_update(&sha, key, kKeyLen);
    const uint8_t salt[] = "ARCANA";
    mbedtls_sha256_update(&sha, salt, 6);
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);

    memcpy(mNoncePrefix, hash, kNoncePrefixLen);

    mTxCounter = 0;
    mRxCounter = 0;
    mRxCounterInitialized = false;
    mInitialized = true;
    return true;
}

void CryptoEngine::buildNonce(uint32_t counter, uint8_t nonce[kNonceLen]) const {
    memcpy(nonce, mNoncePrefix, kNoncePrefixLen);
    nonce[kNoncePrefixLen + 0] = static_cast<uint8_t>(counter & 0xFF);
    nonce[kNoncePrefixLen + 1] = static_cast<uint8_t>((counter >> 8) & 0xFF);
    nonce[kNoncePrefixLen + 2] = static_cast<uint8_t>((counter >> 16) & 0xFF);
    nonce[kNoncePrefixLen + 3] = static_cast<uint8_t>((counter >> 24) & 0xFF);
}

bool CryptoEngine::encrypt(const uint8_t* plain, size_t plainLen,
                            uint8_t* out, size_t outBufSize, size_t& outLen) {
    if (!mInitialized) return false;

    size_t needed = kCounterLen + plainLen + kTagLen;
    if (outBufSize < needed) return false;
    if (mTxCounter == UINT32_MAX) return false; // nonce exhaustion

    uint32_t counter = mTxCounter++;

    // Write counter (LE)
    out[0] = static_cast<uint8_t>(counter & 0xFF);
    out[1] = static_cast<uint8_t>((counter >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((counter >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((counter >> 24) & 0xFF);

    uint8_t nonce[kNonceLen];
    buildNonce(counter, nonce);

    uint8_t* ciphertext = out + kCounterLen;
    uint8_t* tag = out + kCounterLen + plainLen;

    int ret = mbedtls_ccm_encrypt_and_tag(&mCtx, plainLen,
                                           nonce, kNonceLen,
                                           nullptr, 0,
                                           plain, ciphertext,
                                           tag, kTagLen);
    if (ret != 0) return false;

    outLen = needed;
    return true;
}

bool CryptoEngine::decrypt(const uint8_t* in, size_t inLen,
                            uint8_t* plain, size_t plainBufSize, size_t& plainLen) {
    if (!mInitialized) return false;
    if (inLen < kOverhead) return false;

    size_t ciphertextLen = inLen - kOverhead;
    if (plainBufSize < ciphertextLen) return false;

    // Read counter (LE)
    uint32_t counter = static_cast<uint32_t>(in[0])
                     | (static_cast<uint32_t>(in[1]) << 8)
                     | (static_cast<uint32_t>(in[2]) << 16)
                     | (static_cast<uint32_t>(in[3]) << 24);

    // Replay protection
    if (mRxCounterInitialized && counter <= mRxCounter) {
        return false;
    }

    uint8_t nonce[kNonceLen];
    buildNonce(counter, nonce);

    const uint8_t* ciphertext = in + kCounterLen;
    const uint8_t* tag = in + kCounterLen + ciphertextLen;

    int ret = mbedtls_ccm_auth_decrypt(&mCtx, ciphertextLen,
                                        nonce, kNonceLen,
                                        nullptr, 0,
                                        ciphertext, plain,
                                        tag, kTagLen);
    if (ret != 0) return false;

    // Update watermark only after successful decrypt+auth
    mRxCounter = counter;
    mRxCounterInitialized = true;

    plainLen = ciphertextLen;
    return true;
}

bool CryptoEngine::hexToKey(const char* hex, uint8_t key[kKeyLen]) {
    if (!hex || strlen(hex) != kKeyLen * 2) return false;
    for (size_t i = 0; i < kKeyLen; ++i) {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        int hiVal = (hi >= '0' && hi <= '9') ? hi - '0' :
                    (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 :
                    (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : -1;
        int loVal = (lo >= '0' && lo <= '9') ? lo - '0' :
                    (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 :
                    (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : -1;
        if (hiVal < 0 || loVal < 0) return false;
        key[i] = static_cast<uint8_t>((hiVal << 4) | loVal);
    }
    return true;
}

} // namespace arcana
