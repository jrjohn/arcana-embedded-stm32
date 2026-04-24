#pragma once

#include <cstdint>

namespace arcana {

/**
 * @brief OTA firmware update service interface
 *
 * Downloads firmware from HTTP server via ESP8266,
 * writes to SD card, verifies CRC-32, sets OTA flag, and resets.
 */
class OtaService {
public:
    virtual ~OtaService() = default;

    /**
     * @brief Start OTA update (blocks until complete or failed)
     * @param host  Server hostname/IP
     * @param port  Server port (typically 80)
     * @param path  HTTP path (e.g. "/firmware.bin")
     * @param expectedSize  Expected firmware size in bytes
     * @param expectedCrc32 Expected CRC-32 (IEEE) of firmware
     * @return true if download + verify succeeded and system will reset
     */
    virtual bool startUpdate(const char* host, uint16_t port,
                             const char* path, uint32_t expectedSize,
                             uint32_t expectedCrc32) = 0;

    /** @brief Get download progress (0-100) */
    virtual uint8_t getProgress() const = 0;

    /** @brief Check if OTA is currently in progress */
    virtual bool isActive() const = 0;
};

} // namespace arcana
