#include "FlashBlockDevice.hpp"
#include <cstring>

namespace arcana {
namespace storage {

FlashBlockDevice::FlashBlockDevice()
    : mConfig()
    , mReadBuf{}
    , mProgBuf{}
    , mLookaheadBuf{}
{
}

FlashBlockDevice& FlashBlockDevice::getInstance() {
    static FlashBlockDevice sInstance;
    return sInstance;
}

lfs_config* FlashBlockDevice::getConfig() {
    memset(&mConfig, 0, sizeof(mConfig));

    mConfig.context     = this;
    mConfig.read        = flashRead;
    mConfig.prog        = flashProg;
    mConfig.erase       = flashErase;
    mConfig.sync        = flashSync;

    mConfig.read_size   = READ_SIZE;
    mConfig.prog_size   = PROG_SIZE;
    mConfig.block_size  = PAGE_SIZE;
    mConfig.block_count = BLOCK_COUNT;
    mConfig.cache_size  = CACHE_SIZE;
    mConfig.lookahead_size = LOOKAHEAD_SIZE;
    mConfig.block_cycles   = 500;  // Wear leveling cycle count

    mConfig.read_buffer      = mReadBuf;
    mConfig.prog_buffer      = mProgBuf;
    mConfig.lookahead_buffer = mLookaheadBuf;

    return &mConfig;
}

int FlashBlockDevice::flashRead(const struct lfs_config* c, lfs_block_t block,
                                 lfs_off_t off, void* buffer, lfs_size_t size) {
    uint32_t addr = FLASH_BASE_ADDR + (block * PAGE_SIZE) + off;
    memcpy(buffer, reinterpret_cast<const void*>(addr), size);
    return LFS_ERR_OK;
}

int FlashBlockDevice::flashProg(const struct lfs_config* c, lfs_block_t block,
                                 lfs_off_t off, const void* buffer, lfs_size_t size) {
    uint32_t addr = FLASH_BASE_ADDR + (block * PAGE_SIZE) + off;

    HAL_FLASH_Unlock();

    // Program word by word (STM32F1 supports half-word/word writes)
    for (uint32_t i = 0; i < size; i += 2) {
        uint16_t halfword;
        if (i + 1 < size) {
            halfword = static_cast<const uint8_t*>(buffer)[i] |
                       (static_cast<const uint8_t*>(buffer)[i + 1] << 8);
        } else {
            halfword = static_cast<const uint8_t*>(buffer)[i] | 0xFF00;
        }

        HAL_StatusTypeDef status = HAL_FLASH_Program(
            FLASH_TYPEPROGRAM_HALFWORD, addr + i, halfword);

        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return LFS_ERR_IO;
        }
    }

    HAL_FLASH_Lock();
    return LFS_ERR_OK;
}

int FlashBlockDevice::flashErase(const struct lfs_config* c, lfs_block_t block) {
    uint32_t addr = FLASH_BASE_ADDR + (block * PAGE_SIZE);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef eraseInit;
    eraseInit.TypeErase   = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = addr;
    eraseInit.NbPages     = 1;

    uint32_t pageError = 0;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&eraseInit, &pageError);

    HAL_FLASH_Lock();

    return (status == HAL_OK) ? LFS_ERR_OK : LFS_ERR_IO;
}

int FlashBlockDevice::flashSync(const struct lfs_config* c) {
    return LFS_ERR_OK;
}

} // namespace storage
} // namespace arcana
