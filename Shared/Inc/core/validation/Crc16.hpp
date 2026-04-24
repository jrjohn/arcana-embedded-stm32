/**
 * @file Crc16.hpp
 * @brief Software CRC-16 (header-only)
 *
 * Bitwise CRC-16/CCITT reflected implementation.
 * Polynomial 0x8408 — matches esp_crc16_le().
 */

#ifndef ARCANA_CRC16_HPP
#define ARCANA_CRC16_HPP

#include <cstdint>
#include <cstddef>

namespace arcana {

/**
 * @brief Compute CRC-16 over a byte buffer
 * @param init Initial CRC value (typically 0)
 * @param data Pointer to data
 * @param len  Number of bytes
 * @return CRC-16 result
 */
inline uint16_t crc16(uint16_t init, const uint8_t* data, size_t len) {
    uint16_t crc = init;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0x8408;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

} // namespace arcana

#endif /* ARCANA_CRC16_HPP */
