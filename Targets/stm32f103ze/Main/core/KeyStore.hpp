#pragma once

/**
 * @file KeyStore.hpp
 * @brief Read provisioned fleet master secret from internal flash key pages
 *
 * Flash layout (last 4KB of 512KB):
 *   Page A: 0x0807F000 (2KB, primary)
 *   Page B: 0x0807F800 (2KB, backup)
 *
 * Key page format (64 bytes):
 *   [0x00]  4B   Magic "KEY1" (0x4B455931)
 *   [0x04]  1B   Version (1)
 *   [0x05]  1B   Key length (32)
 *   [0x06]  2B   Flags (bit0: provisioned)
 *   [0x08] 32B   Fleet master secret
 *   [0x28]  4B   Provisioning timestamp
 *   [0x2C]  4B   Serial number
 *   [0x30]  4B   CRC-32 of bytes [0x00..0x2F]
 *   [0x34] 12B   Reserved (0xFF)
 *
 * Provisioned by external tool (provision_key.py) via OpenOCD.
 * RDP Level 1 protects against debug-port readout.
 */

#include "Crc32.hpp"
#include <cstdint>
#include <cstring>

namespace arcana {
namespace crypto {

struct __attribute__((packed)) KeyPage {
    uint32_t magic;
    uint8_t  version;
    uint8_t  keyLen;
    uint16_t flags;
    uint8_t  secret[32];
    uint32_t timestamp;
    uint32_t serial;
    uint32_t crc32;
    uint8_t  reserved[12];
};
static_assert(sizeof(KeyPage) == 64, "KeyPage must be 64 bytes");

class KeyStore {
public:
    static constexpr uint32_t MAGIC  = 0x4B455931u;  // "KEY1"
    static constexpr uint32_t PAGE_A = 0x0807F000u;
    static constexpr uint32_t PAGE_B = 0x0807F800u;
    static constexpr uint16_t FLAG_PROVISIONED = 0x0001;

    /**
     * @brief Read fleet master secret from flash key pages
     * @param secret Output buffer (32 bytes)
     * @return true if provisioned key found, false if unprogrammed
     *
     * Tries Page A first, falls back to Page B.
     * On false, secret[] is untouched — caller should use legacy key.
     */
    static bool readSecret(uint8_t secret[32]) {
        if (readPage(PAGE_A, secret)) return true;
        if (readPage(PAGE_B, secret)) return true;
        return false;
    }

private:
    static bool readPage(uint32_t addr, uint8_t secret[32]) {
        const auto* page = reinterpret_cast<const KeyPage*>(addr);

        if (page->magic != MAGIC) return false;
        if (page->version != 1 || page->keyLen != 32) return false;
        if (!(page->flags & FLAG_PROVISIONED)) return false;

        // CRC-32 IEEE of bytes [0x00..0x2F] (48 bytes, everything before crc32 field)
        uint32_t computed = ~crc32(0xFFFFFFFF,
                                   reinterpret_cast<const uint8_t*>(page), 48);
        if (computed != page->crc32) return false;

        memcpy(secret, page->secret, 32);
        return true;
    }
};

} // namespace crypto
} // namespace arcana
