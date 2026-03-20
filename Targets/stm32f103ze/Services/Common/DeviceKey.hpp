#pragma once

/**
 * @file DeviceKey.hpp
 * @brief Per-device encryption key derivation using STM32 hardware UID
 *
 * Key = ChaCha20(master_secret, device_uid_as_nonce, counter=0, 32_zero_bytes)
 *
 * Master secret source (Kerckhoffs' principle):
 *   - Provisioned: fleet master from flash key pages (KeyStore)
 *   - Unprovisioned: legacy hardcoded fallback (dev boards only)
 *
 * The fleet master is also used as the ATS header encryption key.
 */

#include "ChaCha20.hpp"
#include "KeyStore.hpp"
#include <cstring>
#include <stdint.h>

// UID_BASE is defined by CMSIS device headers for each STM32 series:
//   STM32F0xx: 0x1FFFF7AC  (stm32f051x8.h)
//   STM32F1xx: 0x1FFFF7E8  (stm32f103xe.h)
//   STM32F4xx: 0x1FFF7A10
//   STM32L0xx: 0x1FF80050
#ifndef UID_BASE
  #error "UID_BASE not defined. Include the CMSIS device header before DeviceKey.hpp"
#endif

namespace arcana {
namespace crypto {

class DeviceKey {
public:
    static const uint8_t UID_SIZE = 12;  // 96-bit = ChaCha20 nonce size

    /// Read the 12-byte hardware UID (address from CMSIS UID_BASE)
    static void getUID(uint8_t uid[UID_SIZE]) {
        const uint8_t* addr = reinterpret_cast<const uint8_t*>(UID_BASE);
        memcpy(uid, addr, UID_SIZE);
    }

    /**
     * @brief Derive a 256-bit per-device key from master secret + hardware UID
     *
     * Also caches the fleet master for header encryption (getFleetKey()).
     * Sets provisioned flag — check with isProvisioned().
     */
    static void deriveKey(uint8_t deviceKey[ChaCha20::KEY_SIZE]) {
        sProvisioned = KeyStore::readSecret(sFleetKey);

        if (!sProvisioned) {
            // Legacy fallback for unprovisioned dev boards
            static const uint8_t legacy[ChaCha20::KEY_SIZE] = {
                0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
                0xA5, 0x5A, 0x0F, 0xF0, 0x12, 0x34, 0x56, 0x78,
                0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22, 0x33, 0x44
            };
            memcpy(sFleetKey, legacy, ChaCha20::KEY_SIZE);
        }

        uint8_t uid[UID_SIZE];
        getUID(uid);

        // device_key = ChaCha20_keystream(master, uid, counter=0)[:32]
        memset(deviceKey, 0, ChaCha20::KEY_SIZE);
        ChaCha20::crypt(sFleetKey, uid, 0, deviceKey, ChaCha20::KEY_SIZE);
    }

    /// True if fleet master was read from flash key pages
    static bool isProvisioned() { return sProvisioned; }

    /// Fleet master key (for ATS header encryption). Valid after deriveKey().
    static const uint8_t* getFleetKey() { return sFleetKey; }

private:
    static inline bool sProvisioned = false;
    static inline uint8_t sFleetKey[ChaCha20::KEY_SIZE] = {};
};

} // namespace crypto
} // namespace arcana
