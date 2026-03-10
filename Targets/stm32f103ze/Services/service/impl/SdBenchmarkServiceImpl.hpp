#pragma once

#include "SdBenchmarkService.hpp"
#include "SdCard.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace sdbench {

class SdBenchmarkServiceImpl : public SdBenchmarkService {
public:
    static SdBenchmarkService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

private:
    SdBenchmarkServiceImpl();
    ~SdBenchmarkServiceImpl();
    SdBenchmarkServiceImpl(const SdBenchmarkServiceImpl&);
    SdBenchmarkServiceImpl& operator=(const SdBenchmarkServiceImpl&);

    static void benchmarkTask(void* param);
    void runBenchmark();
    void fillAndEncryptBuffer(uint8_t* buf, uint32_t baseRecord);
    void publishStats();

    SdCard& mSd;
    bool mRunning;

    // Block count (for capacity display)
    uint32_t mBlockCount;

    // Format work buffer (reused only during mkfs, then released)
    static const uint32_t MKFS_BUF_SIZE = 4096;
    uint8_t mMkfsBuf[MKFS_BUF_SIZE];

    // Stats tracking
    Observable<SdBenchmarkModel> mStatsObs;
    SdBenchmarkModel mStats;
    uint32_t mWindowStartTick;
    uint32_t mBytesInWindow;
    uint32_t mRecordsInWindow;
    uint32_t mTotalBytesWritten;
    uint32_t mTotalRecords;

    // Dedicated task
    static const uint16_t TASK_STACK_SIZE = 512;
    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
};

} // namespace sdbench
} // namespace arcana
