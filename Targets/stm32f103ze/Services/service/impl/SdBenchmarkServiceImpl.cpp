#include "SdBenchmarkServiceImpl.hpp"
#include "ChaCha20.hpp"
#include "DeviceKey.hpp"
#include <cstring>
#include "stm32f1xx_hal.h"

// DWT cycle counter for accurate timing (works with IRQs disabled)
#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004)
#define DEMCR      (*(volatile uint32_t*)0xE000EDFC)

namespace arcana {
namespace sdbench {

static const uint32_t RECORD_SIZE = 13;  // Same as StorageService

// Per-device encryption key (derived from master secret + hardware UID)
static uint8_t sKey[crypto::ChaCha20::KEY_SIZE];
static bool sKeyDerived = false;

SdBenchmarkServiceImpl::SdBenchmarkServiceImpl()
    : mSd(SdCard::getInstance())
    , mRunning(false)
    , mBlockAddr(0)
    , mBlockCount(0)
    , mWriteBuf{{}}
    , mStatsObs("SdBench Stats")
    , mStats()
    , mWindowStartTick(0)
    , mBytesInWindow(0)
    , mRecordsInWindow(0)
    , mTotalBytesWritten(0)
    , mTotalRecords(0)
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
{
    output.StatsEvents = &mStatsObs;
}

SdBenchmarkServiceImpl::~SdBenchmarkServiceImpl() {
    stop();
}

SdBenchmarkService& SdBenchmarkServiceImpl::getInstance() {
    static SdBenchmarkServiceImpl sInstance;
    return sInstance;
}

ServiceStatus SdBenchmarkServiceImpl::initHAL() {
    // Derive per-device encryption key from hardware UID (once)
    if (!sKeyDerived) {
        crypto::DeviceKey::deriveKey(sKey);
        sKeyDerived = true;
    }

    if (!mSd.initHAL()) {
        mStats.error = true;
        mStats.errorStep = 1;
        publishStats();
        return ServiceStatus::Error;
    }

    // Enable DWT cycle counter for accurate timing
    DEMCR |= (1U << 24);   // TRCENA
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1U;        // CYCCNTENA

    return ServiceStatus::OK;
}

ServiceStatus SdBenchmarkServiceImpl::init() {
    mStats.recordsPerBuf = WRITE_BUF_SIZE / RECORD_SIZE;

    // Get card capacity for wrap-around
    mBlockCount = mSd.getBlockCount();
    if (mBlockCount == 0) {
        mStats.error = true;
        mStats.errorStep = 2;
        publishStats();
        return ServiceStatus::Error;
    }

    // Start writing past MBR/filesystem area
    mBlockAddr = 2048;

    return ServiceStatus::OK;
}

ServiceStatus SdBenchmarkServiceImpl::start() {
    if (!mSd.isReady()) {
        mStats.error = true;
        mStats.errorStep = 6;
        publishStats();
        return ServiceStatus::Error;
    }

    mRunning = true;
    mBytesInWindow = 0;
    mRecordsInWindow = 0;
    mTotalBytesWritten = 0;
    mTotalRecords = 0;

    mTaskHandle = xTaskCreateStatic(
        benchmarkTask, "SdBench", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 2, mTaskStack, &mTaskBuffer);
    if (!mTaskHandle) return ServiceStatus::Error;

    return ServiceStatus::OK;
}

void SdBenchmarkServiceImpl::stop() {
    mRunning = false;
    if (mTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        mTaskHandle = 0;
    }
}

void SdBenchmarkServiceImpl::benchmarkTask(void* param) {
    SdBenchmarkServiceImpl* self = static_cast<SdBenchmarkServiceImpl*>(param);
    vTaskDelay(pdMS_TO_TICKS(15000));  // Wait for MQTT to connect first (SDIO DMA interferes with UART)
    self->runBenchmark();
    vTaskDelete(0);
}

void SdBenchmarkServiceImpl::fillAndEncryptBuffer(uint8_t* buf, uint32_t baseRecord) {
    float temp = 25.5f;
    float hum = 60.0f;
    uint8_t quality = 1;

    uint32_t offset = 0;
    uint32_t idx = 0;
    while (offset + RECORD_SIZE <= WRITE_BUF_SIZE) {
        uint32_t ts = baseRecord + idx;

        buf[offset + 0] = (ts >>  0) & 0xFF;
        buf[offset + 1] = (ts >>  8) & 0xFF;
        buf[offset + 2] = (ts >> 16) & 0xFF;
        buf[offset + 3] = (ts >> 24) & 0xFF;

        uint32_t t;
        memcpy(&t, &temp, 4);
        buf[offset + 4] = (t >>  0) & 0xFF;
        buf[offset + 5] = (t >>  8) & 0xFF;
        buf[offset + 6] = (t >> 16) & 0xFF;
        buf[offset + 7] = (t >> 24) & 0xFF;

        uint32_t h;
        memcpy(&h, &hum, 4);
        buf[offset +  8] = (h >>  0) & 0xFF;
        buf[offset +  9] = (h >>  8) & 0xFF;
        buf[offset + 10] = (h >> 16) & 0xFF;
        buf[offset + 11] = (h >> 24) & 0xFF;

        buf[offset + 12] = quality;
        offset += RECORD_SIZE;
        idx++;
    }

    memset(&buf[offset], 0, WRITE_BUF_SIZE - offset);

    uint8_t nonce[crypto::ChaCha20::NONCE_SIZE];
    memset(nonce, 0, crypto::ChaCha20::NONCE_SIZE);
    nonce[0] = (baseRecord >>  0) & 0xFF;
    nonce[1] = (baseRecord >>  8) & 0xFF;
    nonce[2] = (baseRecord >> 16) & 0xFF;
    nonce[3] = (baseRecord >> 24) & 0xFF;
    crypto::ChaCha20::crypt(sKey, nonce, 0, buf, WRITE_BUF_SIZE);
}

void SdBenchmarkServiceImpl::runBenchmark() {
    uint32_t recordsPerBuf = mStats.recordsPerBuf;
    uint32_t cycleStart = DWT_CYCCNT;
    const uint32_t cpuHz = SystemCoreClock;  // 72MHz

    uint8_t active = 0;

    // Fill first buffer before entering pipeline
    fillAndEncryptBuffer(mWriteBuf[0], mTotalRecords);

    while (mRunning) {
        // Start DMA write of current buffer
        if (!mSd.startWrite(mWriteBuf[active], mBlockAddr, BLOCKS_PER_WRITE)) {
            mStats.error = true;
            mStats.errorStep = 7;
            mStats.errorCode = static_cast<uint8_t>(mSd.getLastError() & 0xFF);
            publishStats();
            vTaskDelay(pdMS_TO_TICKS(1000));
            mBlockAddr = 2048;
            continue;
        }

        // While DMA writes current buffer, encrypt next buffer on CPU
        uint8_t next = 1 - active;
        uint32_t nextRecords = mTotalRecords + recordsPerBuf;
        fillAndEncryptBuffer(mWriteBuf[next], nextRecords);

        // Wait for DMA to finish
        if (!mSd.waitWrite()) {
            mStats.error = true;
            mStats.errorStep = 7;
            mStats.errorCode = static_cast<uint8_t>(mSd.getLastError() & 0xFF);
            publishStats();
            vTaskDelay(pdMS_TO_TICKS(1000));
            mBlockAddr = 2048;
            active = next;
            mTotalRecords = nextRecords;
            continue;
        }

        // Yield to let UART/MQTT task process between DMA bursts
        vTaskDelay(pdMS_TO_TICKS(2));

        // Update counters
        mBlockAddr += BLOCKS_PER_WRITE;
        if (mBlockAddr + BLOCKS_PER_WRITE > mBlockCount) {
            mBlockAddr = 2048;
        }

        mBytesInWindow += WRITE_BUF_SIZE;
        mRecordsInWindow += recordsPerBuf;
        mTotalBytesWritten += WRITE_BUF_SIZE;
        mTotalRecords = nextRecords;
        active = next;

        // Publish stats every ~1 second (DWT cycle counter)
        uint32_t cycleNow = DWT_CYCCNT;
        uint32_t elapsedCycles = cycleNow - cycleStart;
        uint32_t elapsedMs = elapsedCycles / (cpuHz / 1000);

        if (elapsedMs >= 1000) {
            if (elapsedMs > 0) {
                uint64_t num = (uint64_t)mBytesInWindow * 10000ULL;
                uint64_t den = (uint64_t)elapsedMs * 1024ULL;
                mStats.speedKBps10 = (uint32_t)(num / den);
            }
            mStats.totalKB = mTotalBytesWritten / 1024;
            mStats.totalRecords = mTotalRecords;
            mStats.recordsPerSec = mRecordsInWindow;
            mStats.error = false;

            publishStats();

            mBytesInWindow = 0;
            mRecordsInWindow = 0;
            cycleStart = DWT_CYCCNT;
        }
    }
}

void SdBenchmarkServiceImpl::publishStats() {
    mStats.updateTimestamp();
    mStatsObs.publish(&mStats);
}

} // namespace sdbench
} // namespace arcana
