#pragma once

#include "lfs.h"
#include "stm32f1xx_hal.h"

namespace arcana {
namespace storage {

/**
 * STM32F103ZE Internal Flash block device for littlefs.
 *
 * Flash layout (512KB total):
 *   0x0800_0000 - 0x0805_FFFF : Code (384KB)
 *   0x0806_0000 - 0x0807_FFFF : littlefs (128KB = 64 pages × 2KB)
 *
 * STM32F103ZE (high-density): page size = 2KB
 */
class FlashBlockDevice {
public:
    // Flash geometry
    static const uint32_t FLASH_BASE_ADDR  = 0x08060000;  // Start of FS region
    static const uint32_t FLASH_SIZE       = 128 * 1024;  // 128KB
    static const uint32_t PAGE_SIZE        = 2048;         // 2KB per page (high-density)
    static const uint32_t BLOCK_COUNT      = FLASH_SIZE / PAGE_SIZE;  // 64 blocks
    static const uint32_t READ_SIZE        = 1;
    static const uint32_t PROG_SIZE        = 4;            // Word-aligned writes
    static const uint32_t CACHE_SIZE       = 256;
    static const uint32_t LOOKAHEAD_SIZE   = 16;

    static FlashBlockDevice& getInstance();

    // Get configured lfs_config (call once, reuse pointer)
    lfs_config* getConfig();

private:
    FlashBlockDevice();

    // littlefs block device callbacks
    static int flashRead(const struct lfs_config* c, lfs_block_t block,
                         lfs_off_t off, void* buffer, lfs_size_t size);
    static int flashProg(const struct lfs_config* c, lfs_block_t block,
                         lfs_off_t off, const void* buffer, lfs_size_t size);
    static int flashErase(const struct lfs_config* c, lfs_block_t block);
    static int flashSync(const struct lfs_config* c);

    lfs_config mConfig;

    // Static buffers for littlefs (no malloc)
    uint8_t mReadBuf[CACHE_SIZE];
    uint8_t mProgBuf[CACHE_SIZE];
    uint8_t mLookaheadBuf[LOOKAHEAD_SIZE];
};

} // namespace storage
} // namespace arcana
