/**
 * @file DeviceKey.hpp (host test stub)
 *
 * The real DeviceKey.hpp lives in Targets/stm32f103ze/Services/Common/ and
 * `#include "KeyStore.hpp"` resolves to KeyStore.hpp in that same directory
 * (C++ quoted-include same-dir-first rule), which dereferences absolute
 * flash addresses 0x0807F000 / 0x0807F800 and segfaults on host.
 *
 * Consumers (e.g. CommandBridgeImpl.cpp) include "DeviceKey.hpp" via -I
 * lookup since they're in a different directory; placing this stub in
 * MOCKS_DIR (first on the include path) wins, and this header never pulls
 * in KeyStore at all → host build is safe.
 *
 * Behavior matches the real header except KeyStore is bypassed: deriveKey()
 * always uses the legacy fallback master, so derived keys are deterministic
 * across test runs (good for KAT-style assertions).
 */
#pragma once

#include "ChaCha20.hpp"
#include <cstdint>
#include <cstring>

#ifndef UID_BASE
#  error "UID_BASE not defined. Include stm32f1xx_hal.h before DeviceKey.hpp"
#endif

namespace arcana {
namespace crypto {

class DeviceKey {
public:
    static const uint8_t UID_SIZE = 12;

    static void getUID(uint8_t uid[UID_SIZE]) {
        const uint8_t* addr = reinterpret_cast<const uint8_t*>(UID_BASE);
        std::memcpy(uid, addr, UID_SIZE);
    }

    static void deriveKey(uint8_t deviceKey[ChaCha20::KEY_SIZE]) {
        sProvisioned = false;  // host: never provisioned

        static const uint8_t legacy[ChaCha20::KEY_SIZE] = {
            0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
            0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
            0xA5, 0x5A, 0x0F, 0xF0, 0x12, 0x34, 0x56, 0x78,
            0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22, 0x33, 0x44
        };
        std::memcpy(sFleetKey, legacy, ChaCha20::KEY_SIZE);

        uint8_t uid[UID_SIZE];
        getUID(uid);

        std::memset(deviceKey, 0, ChaCha20::KEY_SIZE);
        ChaCha20::crypt(sFleetKey, uid, 0, deviceKey, ChaCha20::KEY_SIZE);
    }

    static bool isProvisioned() { return sProvisioned; }
    static const uint8_t* getFleetKey() { return sFleetKey; }

private:
    static inline bool sProvisioned = false;
    static inline uint8_t sFleetKey[ChaCha20::KEY_SIZE] = {};
};

} // namespace crypto
} // namespace arcana
