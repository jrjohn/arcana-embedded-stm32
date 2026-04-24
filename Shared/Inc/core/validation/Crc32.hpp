/**
 * @file Crc32.hpp
 * @brief Software CRC-32 (header-only, C/C++ compatible)
 *
 * Bitwise CRC-32 IEEE 802.3 reflected implementation.
 * No lookup table - saves 1KB ROM.
 * Polynomial 0xEDB88320 (reflected).
 *
 * Shared between App (C++) and Bootloader (C).
 *
 * Standard IEEE result: ~crc32(0xFFFFFFFF, data, len)
 */

#ifndef ARCANA_CRC32_HPP
#define ARCANA_CRC32_HPP

#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
extern "C" {
#else
#include <stdint.h>
#include <stddef.h>
#endif

/**
 * @brief Compute CRC-32 over a byte buffer
 * @param init Initial CRC value (typically 0xFFFFFFFF)
 * @param data Pointer to data
 * @param len  Number of bytes
 * @return CRC-32 accumulator (caller does ~result for standard IEEE)
 */
static inline uint32_t crc32_calc(uint32_t init, const uint8_t* data, size_t len) {
    uint32_t crc = init;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x00000001u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

#ifdef __cplusplus
} /* extern "C" */

namespace arcana {

inline uint32_t crc32(uint32_t init, const uint8_t* data, size_t len) {
    return crc32_calc(init, data, len);
}

} // namespace arcana
#endif

#endif /* ARCANA_CRC32_HPP */
