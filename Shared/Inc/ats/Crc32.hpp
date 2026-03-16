/**
 * @file Crc32.hpp
 * @brief Software CRC-32 (header-only)
 *
 * Bitwise CRC-32 IEEE 802.3 reflected implementation.
 * No lookup table — saves 1KB ROM.
 * Polynomial 0xEDB88320 (reflected).
 *
 * Standard IEEE result: ~crc32(0xFFFFFFFF, data, len)
 */

#ifndef ARCANA_ATS_CRC32_HPP
#define ARCANA_ATS_CRC32_HPP

#include <cstdint>
#include <cstddef>

namespace arcana {
namespace ats {

/**
 * @brief Compute CRC-32 over a byte buffer
 * @param init Initial CRC value (typically 0xFFFFFFFF)
 * @param data Pointer to data
 * @param len  Number of bytes
 * @return CRC-32 accumulator (caller does ~result for standard IEEE)
 */
inline uint32_t crc32(uint32_t init, const uint8_t* data, size_t len) {
    uint32_t crc = init;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x00000001) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

} // namespace ats
} // namespace arcana

#endif /* ARCANA_ATS_CRC32_HPP */
