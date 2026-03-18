#pragma once
/**
 * @file EspFlasher.hpp
 * @brief Flash ESP8266 firmware from SD card via USART3 bootloader protocol
 *
 * SLIP protocol: SYNC -> FLASH_BEGIN -> FLASH_DATA x N -> FLASH_END
 *
 * Usage:
 * 1. Place esp_fw/ files on SD card (bootloader.bin, esp-at.bin, etc.)
 * 2. Short J83 Pin 3-4 (GPIO0=GND) on board
 * 3. Call EspFlasher::run() after SD mounted - runs in dedicated 4KB task
 */

#include <cstdint>

namespace arcana {

class EspFlasher {
public:
    /**
     * @brief Check for ESP firmware on SD, flash if bootloader detected
     * Spawns a dedicated FreeRTOS task (4KB stack) to avoid overflow.
     * Blocks until complete or skipped.
     * @return true if flashing attempted, false if skipped
     */
    static bool run();

    static constexpr uint16_t BLOCK_SIZE = 512;  /* Reduced to save RAM */

private:
    static bool doFlash();
    static bool flashPartition(const char* path, uint32_t offset);

    /* SLIP framing */
    static void slipBegin();
    static void slipSend(const uint8_t* data, uint16_t len);
    static void slipEnd();

    /* Protocol */
    static bool sync();
    static bool flashBegin(uint32_t eraseSize, uint32_t numBlocks,
                           uint32_t blockSize, uint32_t offset);
    static bool flashData(const uint8_t* data, uint32_t dataLen, uint32_t seq);
    static bool flashEnd(bool reboot);

    static bool sendCommand(uint8_t cmd, const uint8_t* payload, uint16_t payloadLen,
                            uint32_t checksum);
    static bool recvResponse(uint8_t expectedCmd, uint32_t timeoutMs);

    /* UART low-level */
    static void uartSendByte(uint8_t b);
    static void uartSend(const uint8_t* data, uint16_t len);
    static int  uartRecvByte(uint32_t timeoutMs);
    static void uartFlushRx();

    /* Helpers */
    static uint32_t checksum(const uint8_t* data, uint32_t len);
    static void espReset();

    static constexpr uint8_t  CMD_SYNC        = 0x08;
    static constexpr uint8_t  CMD_FLASH_BEGIN  = 0x02;
    static constexpr uint8_t  CMD_FLASH_DATA   = 0x03;
    static constexpr uint8_t  CMD_FLASH_END    = 0x04;
    static constexpr uint8_t  SLIP_END         = 0xC0;
    static constexpr uint8_t  SLIP_ESC         = 0xDB;
    static constexpr uint8_t  SLIP_ESC_END     = 0xDC;
    static constexpr uint8_t  SLIP_ESC_ESC     = 0xDD;
};

} // namespace arcana
