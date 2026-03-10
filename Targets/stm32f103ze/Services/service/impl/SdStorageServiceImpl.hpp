#pragma once

#include "SdStorageService.hpp"
#include "SdFalAdapter.hpp"
#include "ChaCha20.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

extern "C" {
#include <flashdb.h>
}

namespace arcana {
namespace sdstorage {

/**
 * SD card TSDB/KVDB storage with ChaCha20 encryption.
 *
 * Observer callback copies sensor data + signals a dedicated FreeRTOS task.
 * Task serializes, encrypts, and appends to FlashDB TSDB.
 *
 * Record blob (26 bytes): [nonce:12][encrypted_payload:14]
 * Payload (14 bytes):     [timestamp:4][temperature:4][accelX:2][accelY:2][accelZ:2]
 */
class SdStorageServiceImpl : public SdStorageService {
public:
    static SdStorageService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    bool exportDailyFile(uint32_t date) override;
    bool isDateUploaded(uint32_t date) override;
    bool markDateUploaded(uint32_t date) override;
    uint16_t queryByDate(uint32_t dateYYYYMMDD,
                         SensorDataModel* out, uint16_t maxCount) override;

private:
    SdStorageServiceImpl();
    ~SdStorageServiceImpl();
    SdStorageServiceImpl(const SdStorageServiceImpl&);
    SdStorageServiceImpl& operator=(const SdStorageServiceImpl&);

    // Observer callback
    static void onSensorData(SensorDataModel* model, void* context);

    // Dedicated task
    static void storageTask(void* param);
    void taskLoop();
    void appendRecord(const SensorDataModel* model);

    // Encrypt/decrypt helpers
    static const uint32_t PAYLOAD_SIZE = 14;
    static const uint32_t BLOB_SIZE    = 12 + PAYLOAD_SIZE;  // nonce + encrypted payload
    void serializePayload(const SensorDataModel* model, uint8_t* buf);
    void deserializePayload(const uint8_t* buf, SensorDataModel* model);
    void makeNonce(uint8_t nonce[12], uint32_t counter, uint32_t tick);

    // Daily export helpers
    bool writeEncFileHeader(FIL* fp, uint32_t recordCount);
    static bool exportIterCb(fdb_tsl_t tsl, void* arg);

    // Query helpers
    struct QueryCtx {
        SdStorageServiceImpl* self;
        SensorDataModel* out;
        uint16_t maxCount;
        uint16_t count;
    };
    static bool queryIterCb(fdb_tsl_t tsl, void* arg);

    void publishStats();

    // FlashDB instances
    struct fdb_tsdb mTsdb;
    struct fdb_kvdb mKvdb;
    bool mDbReady;

    // FAL adapter
    storage::SdFalAdapter& mFal;

    // Per-device encryption key
    static uint8_t sKey[crypto::ChaCha20::KEY_SIZE];

    // Nonce counter (monotonic, persisted via record count)
    uint32_t mNonceCounter;

    // Dedicated task
    static const uint16_t TASK_STACK_SIZE = 1024;
    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;

    // Pending write data
    SensorDataModel mPendingData;
    StaticSemaphore_t mWriteSemBuffer;
    SemaphoreHandle_t mWriteSem;

    // Stats
    Observable<StorageStatsModel> mStatsObs;
    StorageStatsModel mStats;
    uint32_t mWindowStartTick;
    uint16_t mWritesInWindow;
    uint16_t mLastRate;
};

} // namespace sdstorage
} // namespace arcana
