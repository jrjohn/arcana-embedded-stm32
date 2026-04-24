#pragma once

#include <cstdint>
#include <cstddef>
#include "mbedtls/ccm.h"

namespace arcana {

/**
 * AES-256-CCM encryption engine — compatible with ESP32 Arcana CryptoEngine.
 *
 * Envelope: [counter:4 LE][ciphertext:N][tag:8]
 * Nonce: SHA256(key || "ARCANA")[0..8] + counter[4 LE] = 13 bytes
 * Replay protection via monotonic RX counter watermark.
 */
class CryptoEngine {
public:
    CryptoEngine() = default;
    ~CryptoEngine();

    CryptoEngine(const CryptoEngine&) = delete;
    CryptoEngine& operator=(const CryptoEngine&) = delete;

    static constexpr size_t kKeyLen = 32;      // AES-256
    static constexpr size_t kTagLen = 8;
    static constexpr size_t kCounterLen = 4;
    static constexpr size_t kOverhead = kCounterLen + kTagLen; // 12 bytes

    /** Initialize with 32-byte key. Returns true on success. */
    bool init(const uint8_t key[kKeyLen]);

    /** Encrypt: plaintext -> [counter:4 LE][ciphertext:N][tag:8] */
    bool encrypt(const uint8_t* plain, size_t plainLen,
                 uint8_t* out, size_t outBufSize, size_t& outLen);

    /** Decrypt: [counter:4 LE][ciphertext:N][tag:8] -> plaintext */
    bool decrypt(const uint8_t* in, size_t inLen,
                 uint8_t* plain, size_t plainBufSize, size_t& plainLen);

    /** Parse 64-char hex string into 32-byte key */
    static bool hexToKey(const char* hex, uint8_t key[kKeyLen]);

private:
    static constexpr size_t kNoncePrefixLen = 9;
    static constexpr size_t kNonceLen = kNoncePrefixLen + kCounterLen; // 13

    mbedtls_ccm_context mCtx{};
    uint8_t mNoncePrefix[kNoncePrefixLen]{};
    uint32_t mTxCounter = 0;
    uint32_t mRxCounter = 0;
    bool mRxCounterInitialized = false;
    bool mInitialized = false;

    void buildNonce(uint32_t counter, uint8_t nonce[kNonceLen]) const;
};

} // namespace arcana
