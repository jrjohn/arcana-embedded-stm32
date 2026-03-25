#pragma once

#include "CryptoEngine.hpp"
#include "FreeRTOS.h"
#include "semphr.h"
#include <cstdint>
#include <cstddef>

namespace arcana {

/**
 * ECDH P-256 Key Exchange Manager — compatible with ESP32 Arcana protocol.
 *
 * Flow:
 *  1. Client sends KeyExchange request with client public key (64 bytes)
 *  2. Server generates keypair, computes ECDH shared secret
 *  3. Derives session key via HKDF-SHA256(secret, PSK salt, "ARCANA-SESSION")
 *  4. Computes auth tag: HMAC-SHA256(PSK, serverPub || clientPub)
 *  5. Response encrypted with PSK (session not yet installed)
 *  6. After response sent, installs session key
 *  7. Subsequent messages use session key (Perfect Forward Secrecy)
 */
class KeyExchangeManager {
public:
    static constexpr size_t kPubKeyLen = 64;   // P-256 x||y raw coordinates
    static constexpr size_t kAuthTagLen = 32;  // HMAC-SHA256
    static constexpr int kMaxSessions = 2;     // 1 BLE + 1 MQTT (F103 RAM limited)

    KeyExchangeManager();
    ~KeyExchangeManager();

    KeyExchangeManager(const KeyExchangeManager&) = delete;
    KeyExchangeManager& operator=(const KeyExchangeManager&) = delete;

    /** Initialize with PSK. Returns true on success. */
    bool init(const uint8_t psk[CryptoEngine::kKeyLen]);

    /**
     * Perform ECDH key exchange.
     * Generates server keypair, derives session key, computes auth tag.
     * Result staged as "pending" until InstallPendingSession.
     * WARNING: needs ~3KB stack — call from a task with enough stack.
     */
    bool performKeyExchange(uint8_t source, uint16_t connId,
                            const uint8_t clientPub[64],
                            uint8_t serverPub[64], uint8_t authTag[32]);

    /** Install pending session (called AFTER response encrypted+sent with PSK) */
    bool installPendingSession(uint8_t source, uint16_t connId);

    /** Thread-safe decrypt with session key. Returns false → fall back to PSK. */
    bool decryptWithSession(uint8_t source, uint16_t connId,
                            const uint8_t* in, size_t inLen,
                            uint8_t* plain, size_t plainBufSize, size_t& plainLen);

    /** Thread-safe encrypt with session key. Returns false → fall back to PSK. */
    bool encryptWithSession(uint8_t source, uint16_t connId,
                            const uint8_t* plain, size_t plainLen,
                            uint8_t* out, size_t outBufSize, size_t& outLen);

    /** Remove session on disconnect */
    void removeSession(uint8_t source, uint16_t connId);

private:
    struct Session {
        bool active = false;
        uint8_t source = 0;
        uint16_t connId = 0;
        CryptoEngine engine;
    };

    struct PendingSession {
        bool valid = false;
        uint8_t source = 0;
        uint16_t connId = 0;
        uint8_t key[CryptoEngine::kKeyLen] = {};
    };

    Session mSessions[kMaxSessions];
    PendingSession mPending;
    uint8_t mPsk[CryptoEngine::kKeyLen] = {};
    SemaphoreHandle_t mMutex;
    StaticSemaphore_t mMutexBuf;  // static allocation for F103

    // Manual HKDF-SHA256
    bool hkdfSha256(const uint8_t* ikm, size_t ikmLen,
                    const uint8_t* salt, size_t saltLen,
                    const uint8_t* info, size_t infoLen,
                    uint8_t* okm, size_t okmLen);

    bool hmacSha256(const uint8_t* key, size_t keyLen,
                    const uint8_t* data, size_t dataLen,
                    uint8_t out[32]);
};

} // namespace arcana
