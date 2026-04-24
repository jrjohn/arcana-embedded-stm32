#pragma once

#include "stm32f1xx_hal.h"
#include <cstdint>

namespace arcana {

class SdCard {
public:
    static SdCard& getInstance();

    bool initHAL();
    bool isReady() const { return mReady; }

    // Write multiple 512-byte blocks (synchronous: starts DMA + waits)
    bool writeBlocks(const uint8_t* data, uint32_t blockAddr, uint32_t numBlocks);

    // Async DMA write: start transfer, then call waitWrite() to complete
    bool startWrite(const uint8_t* data, uint32_t blockAddr, uint32_t numBlocks);
    bool waitWrite();

    uint32_t getLastError() const;

    uint32_t getBlockSize() const { return BLOCK_SIZE; }
    uint32_t getBlockCount() const;

    static const uint32_t BLOCK_SIZE = 512;

private:
    SdCard();
    ~SdCard();
    SdCard(const SdCard&);
    SdCard& operator=(const SdCard&);

    void initGpio();
    bool mReady;
};

} // namespace arcana
