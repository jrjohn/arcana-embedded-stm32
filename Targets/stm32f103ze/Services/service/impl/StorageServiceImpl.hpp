#pragma once

#include "StorageService.hpp"
#include "FlashBlockDevice.hpp"
#include "ChaCha20.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

namespace arcana {
namespace storage {

/**
 * StorageService: Subscribes to sensor data, encrypts with ChaCha20,
 * writes to internal flash via littlefs.
 *
 * Architecture: Observer callback only copies data and signals a dedicated
 * FreeRTOS task. The task handles flash I/O (ChaCha20 + littlefs) on its
 * own stack, keeping the ObservableDispatcher responsive.
 */
class StorageServiceImpl : public StorageService {
public:
    static StorageService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    uint16_t readRecords(SensorDataModel* out, uint16_t maxCount) override;
    uint32_t getRecordCount() override;

private:
    StorageServiceImpl();
    ~StorageServiceImpl();
    StorageServiceImpl(const StorageServiceImpl&);
    StorageServiceImpl& operator=(const StorageServiceImpl&);

    // Observer callback (lightweight — copies data + signals task)
    static void onSensorData(SensorDataModel* model, void* context);

    // Dedicated task for flash I/O
    static void storageTask(void* param);
    void writeRecord(const SensorDataModel* model);

    // Encrypt/decrypt a single record buffer
    void cryptRecord(uint8_t* buf, uint32_t len, uint32_t recordIndex);

    // Nonce generation from record index
    void makeNonce(uint8_t nonce[crypto::ChaCha20::NONCE_SIZE], uint32_t recordIndex);

    // Record serialization
    static const uint32_t RECORD_SIZE = 14;  // 4+4+2+2+2
    void serializeRecord(const SensorDataModel* model, uint8_t* buf);
    void deserializeRecord(const uint8_t* buf, SensorDataModel* model);

    void publishStats();

    // Filesystem
    lfs_t mLfs;
    FlashBlockDevice& mFlash;
    bool mMounted;
    uint32_t mRecordCount;

    // Static buffers for littlefs file operations
    lfs_file_t mFile;
    uint8_t mFileBuf[FlashBlockDevice::CACHE_SIZE];
    struct lfs_file_config mFileConfig;

    // Per-device encryption key (derived from master secret + hardware UID)
    static uint8_t sKey[crypto::ChaCha20::KEY_SIZE];

    // Dedicated write task (large stack for ChaCha20 + littlefs + HAL_FLASH)
    static const uint16_t TASK_STACK_SIZE = 512;  // 2KB stack
    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;

    // Pending write data (copied from observer callback)
    SensorDataModel mPendingData;
    StaticSemaphore_t mWriteSemBuffer;
    SemaphoreHandle_t mWriteSem;  // Binary semaphore: signals task to write

    // Mutex for filesystem access
    StaticSemaphore_t mMutexBuffer;
    SemaphoreHandle_t mMutex;

    // Stats output
    Observable<StorageStatsModel> mStatsObs;
    StorageStatsModel mStats;

    // Write rate tracking (1-second sliding window)
    uint32_t mWindowStartTick;
    uint16_t mWritesInWindow;
    uint16_t mLastRate;

    static const char* FILENAME;
    static const uint32_t HEADER_SIZE = 4;  // Record count
};

} // namespace storage
} // namespace arcana
