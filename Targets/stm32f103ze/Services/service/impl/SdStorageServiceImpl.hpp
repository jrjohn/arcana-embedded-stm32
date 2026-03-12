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
 *
 * High-frequency ADC batch mode:
 * - Buffers N ADC samples into a single FlashDB blob
 * - Reduces FAL ops/sec for high-rate sensors (ADS1298)
 * - Configurable samples per batch (default: 10)
 */
class SdStorageServiceImpl : public SdStorageService {
public:
    static SdStorageService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    bool configureBatchWrite(uint16_t samplesPerBatch) override;
    bool flushBatch() override;
    bool exportDailyFile(uint32_t date) override;
    bool isDateUploaded(uint32_t date) override;
    bool markDateUploaded(uint32_t date) override;
    uint16_t queryByDate(uint32_t dateYYYYMMDD,
                         SensorDataModel* out, uint16_t maxCount) override;
    uint32_t queryAdcByTimeRange(uint32_t startTime, uint32_t endTime,
                                  AdcDataModel* out, uint16_t maxBatches) override;
    void enableStressTest(uint16_t hz) override;
    void enableAdcStressTest(uint16_t sps, uint16_t batchSize) override;

private:
    SdStorageServiceImpl();
    ~SdStorageServiceImpl();
    SdStorageServiceImpl(const SdStorageServiceImpl&);
    SdStorageServiceImpl& operator=(const SdStorageServiceImpl&);

    // Observer callbacks
    static void onSensorData(SensorDataModel* model, void* context);
    static void onAdcData(AdcDataModel* model, void* context);

    // Dedicated task
    static void storageTask(void* param);
    void taskLoop();
    void appendRecord(const SensorDataModel* model);
    void appendAdcSample(const AdcDataModel* model);
    void flushAdcBatch();

    // Encrypt/decrypt helpers
    static const uint32_t PAYLOAD_SIZE = 14;
    static const uint32_t BLOB_SIZE    = 12 + PAYLOAD_SIZE;  // nonce + encrypted payload
    void serializePayload(const SensorDataModel* model, uint8_t* buf);
    void deserializePayload(const uint8_t* buf, SensorDataModel* model);
    void makeNonce(uint8_t nonce[12], uint32_t counter, uint32_t tick);

    // ADC batch serialization
    static const uint16_t MAX_BATCH_SAMPLES = 100;  // Max samples per batch blob
    static const uint32_t MAX_BATCH_BLOB_SIZE = 12 + (MAX_BATCH_SAMPLES * AdcDataModel::SAMPLE_SIZE);
    void serializeAdcBatch(const uint8_t* samples, uint16_t count, 
                           uint32_t timestamp, uint8_t* buf, uint32_t& outLen);

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
    void updateStats();

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
    static const uint16_t TASK_STACK_SIZE = 1536;  // 6KB (ADC batch blob needs ~2.4KB stack)
    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;

    // Pending write data (SensorData)
    SensorDataModel mPendingSensorData;
    StaticSemaphore_t mWriteSemBuffer;
    SemaphoreHandle_t mWriteSem;
    bool mHasPendingSensorData;

    // ADC batch buffer
    uint8_t mAdcBatchBuffer[MAX_BATCH_SAMPLES * AdcDataModel::SAMPLE_SIZE];
    uint16_t mAdcBatchCount;
    uint16_t mAdcBatchTarget;       // Target samples per batch (configurable)
    uint32_t mAdcBatchFirstTime;    // Timestamp of first sample in batch
    StaticSemaphore_t mAdcSemBuffer;
    SemaphoreHandle_t mAdcSem;
    bool mHasPendingAdcData;
    AdcDataModel mPendingAdcBatch;  // Buffer for incoming ADC batch from ISR/dispatcher

    // Stats
    Observable<StorageStatsModel> mStatsObs;
    StorageStatsModel mStats;
    uint32_t mWindowStartTick;
    uint16_t mWritesInWindow;
    uint16_t mLastRate;
    uint32_t mSamplesInWindow;      // For batch mode rate calculation
    uint16_t mLastSampleRate;

    // Stress test (internal dummy writes, independent of sensor)
    uint16_t mStressTestHz;
    void appendDummyRecord();

    // ADC batch stress test (simulates ADS1298 8ch @ configurable SPS)
    uint16_t mAdcStressTestSps;   // 0 = disabled
    void appendDummyAdcBatch();
};

} // namespace sdstorage
} // namespace arcana
