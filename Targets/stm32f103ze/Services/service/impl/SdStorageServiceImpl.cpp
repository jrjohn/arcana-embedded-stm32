#include "stm32f1xx_hal.h"
#include "SdStorageServiceImpl.hpp"
#include "DeviceKey.hpp"
#include "SystemClock.hpp"
#include "ff.h"
#include <cstring>
#include <cstdio>

// Wait for exFAT mount from SdBenchmarkService
extern "C" {
    extern volatile uint8_t g_exfat_ready;
    extern volatile uint8_t g_sdio_reinit_enabled;
}

namespace arcana {
namespace sdstorage {

uint8_t SdStorageServiceImpl::sKey[crypto::ChaCha20::KEY_SIZE] = {};

// FlashDB lock/unlock callbacks (FreeRTOS mutex)
static SemaphoreHandle_t sDbMutex = 0;
static StaticSemaphore_t sDbMutexBuf;

static void fdbLock(fdb_db_t db) {
    (void)db;
    if (sDbMutex) xSemaphoreTake(sDbMutex, portMAX_DELAY);
}

static void fdbUnlock(fdb_db_t db) {
    (void)db;
    if (sDbMutex) xSemaphoreGive(sDbMutex);
}

// FlashDB timestamp callback — must be strictly monotonic increasing.
// Returns millisecond epoch (fdb_time_t = int64_t with FDB_USING_TIMESTAMP_64BIT).
static fdb_time_t sLastTime = 0;
static fdb_time_t fdbGetTime(void) {
    fdb_time_t now;
    if (SystemClock::getInstance().isSynced()) {
        now = (fdb_time_t)SystemClock::getInstance().nowMs();
    } else {
        now = (fdb_time_t)xTaskGetTickCount();
    }
    if (now <= sLastTime) {
        now = sLastTime + 1;
    }
    sLastTime = now;
    return now;
}

SdStorageServiceImpl::SdStorageServiceImpl()
    : mTsdb()
    , mKvdb()
    , mDbReady(false)
    , mFal(storage::SdFalAdapter::getInstance())
    , mNonceCounter(0)
    , mRecordCount(0)
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
    , mRunning(false)
    , mPendingSensorData()
    , mWriteSemBuffer()
    , mWriteSem(0)
    , mHasPendingSensorData(false)
    , mAdcBatchBuffer{}
    , mAdcBatchCount(0)
    , mAdcBatchTarget(10)      // Default: 10 samples per batch
    , mAdcBatchFirstTime(0)
    , mAdcSemBuffer()
    , mAdcSem(0)
    , mHasPendingAdcData(false)
    , mStatsObs("SdStorage Stats")
    , mStats()
    , mWindowStartTick(0)
    , mWritesInWindow(0)
    , mLastRate(0)
    , mSamplesInWindow(0)
    , mLastSampleRate(0)
    , mConsecErrors(0)
    , mStressTestHz(0)
    , mAdcStressTestSps(0)
{
    input.SensorData = 0;
    input.AdcData = 0;
    output.StatsEvents = &mStatsObs;
}

SdStorageServiceImpl::~SdStorageServiceImpl() {
    stop();
}

SdStorageService& SdStorageServiceImpl::getInstance() {
    static SdStorageServiceImpl sInstance;
    return sInstance;
}

ServiceStatus SdStorageServiceImpl::initHAL() {
    crypto::DeviceKey::deriveKey(sKey);
    return ServiceStatus::OK;
}

ServiceStatus SdStorageServiceImpl::init() {
    mWriteSem = xSemaphoreCreateBinaryStatic(&mWriteSemBuffer);
    if (!mWriteSem) return ServiceStatus::Error;

    mAdcSem = xSemaphoreCreateBinaryStatic(&mAdcSemBuffer);
    if (!mAdcSem) return ServiceStatus::Error;

    sDbMutex = xSemaphoreCreateMutexStatic(&sDbMutexBuf);
    if (!sDbMutex) return ServiceStatus::Error;

    return ServiceStatus::OK;
}

ServiceStatus SdStorageServiceImpl::start() {
    mRunning = true;
    mTaskHandle = xTaskCreateStatic(
        storageTask, "SdStorage", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 3, mTaskStack, &mTaskBuffer);
    if (!mTaskHandle) return ServiceStatus::Error;

    if (input.SensorData) {
        input.SensorData->subscribe(onSensorData, this);
    }
    if (input.AdcData) {
        input.AdcData->subscribe(onAdcData, this);
    }
    return ServiceStatus::OK;
}

void SdStorageServiceImpl::stop() {
    mRunning = false;
    if (mTaskHandle) {
        xSemaphoreGive(mWriteSem);
        xSemaphoreGive(mAdcSem);
        vTaskDelay(pdMS_TO_TICKS(50));
        mTaskHandle = 0;
    }
    if (mDbReady) {
        // Flush any pending ADC batch before shutdown
        if (mAdcBatchCount > 0) {
            flushAdcBatch();
        }
        fdb_tsdb_deinit(&mTsdb);
        fdb_kvdb_deinit(&mKvdb);
        mDbReady = false;
    }
}

void SdStorageServiceImpl::onSensorData(SensorDataModel* model, void* context) {
    SdStorageServiceImpl* self = static_cast<SdStorageServiceImpl*>(context);
    self->mPendingSensorData = *model;
    self->mHasPendingSensorData = true;
    xSemaphoreGive(self->mWriteSem);
}

void SdStorageServiceImpl::onAdcData(AdcDataModel* model, void* context) {
    SdStorageServiceImpl* self = static_cast<SdStorageServiceImpl*>(context);
    
    // Quick check and signal only - don't process in ISR/dispatcher context
    // Actual processing happens in taskLoop to avoid blocking
    if (model && model->sampleCount > 0) {
        // Store reference to current batch (thread-safe as publisher waits)
        self->mPendingAdcBatch = *model;
        self->mHasPendingAdcData = true;
        xSemaphoreGive(self->mAdcSem);
    }
}

bool SdStorageServiceImpl::configureBatchWrite(uint16_t samplesPerBatch) {
    if (samplesPerBatch == 0 || samplesPerBatch > MAX_BATCH_SAMPLES) {
        return false;
    }
    mAdcBatchTarget = samplesPerBatch;
    mStats.batchSize = samplesPerBatch;
    return true;
}

bool SdStorageServiceImpl::flushBatch() {
    if (!mDbReady) return false;
    if (mAdcBatchCount > 0) {
        flushAdcBatch();
        mFal.sync();  // Force sync to SD card
        return true;
    }
    return false;
}

void SdStorageServiceImpl::storageTask(void* param) {
    SdStorageServiceImpl* self = static_cast<SdStorageServiceImpl*>(param);
    uint32_t startTick = xTaskGetTickCount();
    printf("[SdStorage] Task started at tick %lu\n", startTick);

    // Wait for exFAT filesystem to be ready
    int waitCount = 0;
    const int MAX_WAIT = 300;
    printf("[SdStorage] Waiting for exFAT ready...\n");
    while (!g_exfat_ready && self->mRunning && waitCount < MAX_WAIT) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waitCount++;
        if (waitCount % 10 == 0) {
            printf("[SdStorage] Waiting for exFAT... %d/300\n", waitCount);
        }
    }
    uint32_t exfatTick = xTaskGetTickCount();
    printf("[SdStorage] exFAT ready after %lu ms\n", (exfatTick - startTick) * portTICK_PERIOD_MS);
    
    if (!self->mRunning) { vTaskDelete(0); return; }

    // Initialize FAL adapter
    printf("[SdStorage] Initializing FAL adapter...\n");
    uint32_t falStartTick = xTaskGetTickCount();
    if (!self->mFal.init()) {
        printf("[SdStorage] FAL init FAILED!\n");
        vTaskDelete(0);
        return;
    }
    uint32_t falEndTick = xTaskGetTickCount();
    printf("[SdStorage] FAL init OK (%lu ms)\n", (falEndTick - falStartTick) * portTICK_PERIOD_MS);

    // Initialize FlashDB TSDB (virtual sector headers → near-instant init)
    // Note: FDB_TSDB_CTRL_SET_MAX_SIZE is NOT used — _fdb_init unconditionally
    // overwrites db->max_size from partition.len. We update it via setTsdbMaxSizePtr.
    printf("[SdStorage] Initializing TSDB...\n");
    uint32_t secSize = storage::SdFalAdapter::SECTOR_SIZE;

    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_SEC_SIZE, &secSize);
    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_LOCK, (void *)fdbLock);
    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_UNLOCK, (void *)fdbUnlock);

    // Use larger blob size for batch mode (up to MAX_BATCH_SAMPLES * SAMPLE_SIZE + metadata)
    uint32_t maxBlobSize = 12 + (MAX_BATCH_SAMPLES * AdcDataModel::SAMPLE_SIZE) + 32;
    uint32_t tsdbStartTick = xTaskGetTickCount();
    self->mFal.setInitScanActive(true);
    fdb_err_t err = fdb_tsdb_init(&self->mTsdb, "sensor", "tsdb", fdbGetTime,
                                   maxBlobSize, NULL);
    self->mFal.setInitScanActive(false);
    uint32_t tsdbEndTick = xTaskGetTickCount();

    if (err != FDB_NO_ERR) {
        printf("[SdStorage] TSDB init failed: %d\n", err);
        vTaskDelete(0);
        return;
    }
    // Register FlashDB's cached max_size so growTsdbIfNeeded can update it
    self->mFal.setTsdbMaxSizePtr(&self->mTsdb.parent.max_size);
    // nano specs doesn't support %lld — print high:low parts
    printf("[SdStorage] TSDB init OK (%lu ms), last_time=%lu:%lu ms\n",
           (tsdbEndTick - tsdbStartTick) * portTICK_PERIOD_MS,
           (uint32_t)(self->mTsdb.last_time >> 32),
           (uint32_t)(self->mTsdb.last_time & 0xFFFFFFFF));

    // Disable rollover — keep all records, don't overwrite old data
    bool rollover = false;
    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_ROLLOVER, &rollover);

    // Seed timestamp
    if (self->mTsdb.last_time > 0) {
        sLastTime = self->mTsdb.last_time;
    }

    // Initialize FlashDB KVDB (virtual sector headers → near-instant init)
    uint32_t kvMaxSize = storage::SdFalAdapter::KVDB_SIZE;
    fdb_kvdb_control(&self->mKvdb, FDB_KVDB_CTRL_SET_SEC_SIZE, &secSize);
    fdb_kvdb_control(&self->mKvdb, FDB_KVDB_CTRL_SET_MAX_SIZE, &kvMaxSize);
    fdb_kvdb_control(&self->mKvdb, FDB_KVDB_CTRL_SET_LOCK, (void *)fdbLock);
    fdb_kvdb_control(&self->mKvdb, FDB_KVDB_CTRL_SET_UNLOCK, (void *)fdbUnlock);

    printf("[SdStorage] Initializing KVDB...\n");
    uint32_t kvdbStartTick = xTaskGetTickCount();
    err = fdb_kvdb_init(&self->mKvdb, "upload", "kvdb", NULL, NULL);
    uint32_t kvdbEndTick = xTaskGetTickCount();

    if (err != FDB_NO_ERR) {
        printf("[SdStorage] KVDB init failed: %d\n", err);
        fdb_tsdb_deinit(&self->mTsdb);
        vTaskDelete(0);
        return;
    }
    printf("[SdStorage] KVDB init OK (%lu ms)\n",
           (kvdbEndTick - kvdbStartTick) * portTICK_PERIOD_MS);

    self->mDbReady = true;

    uint32_t totalTick = xTaskGetTickCount();
    printf("[SdStorage] ===== Total startup time: %lu ms =====\n", (totalTick - startTick) * portTICK_PERIOD_MS);

    // DEBUG: Check stress test config
    printf("[SdStorage] Task started, mStressTestHz=%d\n", self->mStressTestHz);

    // Nonce counter: use TSDB last_time (ms epoch) / 1000 as base.
    // Nonce is [counter:4][tick:4][0:4] — only needs uniqueness, not precision.
    self->mNonceCounter = (uint32_t)(self->mTsdb.last_time / 1000);
    printf("[SdStorage] Initial nonce counter: %lu (from last_time/1000)\n", self->mNonceCounter);

    // Enable SDIO deep reinit now that boot is complete
    g_sdio_reinit_enabled = 1;
    printf("[SdStorage] SDIO deep reinit enabled\n");

    self->taskLoop();
    vTaskDelete(0);
}

void SdStorageServiceImpl::taskLoop() {
    uint32_t lastDay = 0;
    uint32_t lastSyncTick = xTaskGetTickCount();
    uint32_t lastBatchFlushTick = xTaskGetTickCount();
    uint32_t lastStressTick = xTaskGetTickCount();
    static const uint32_t SYNC_INTERVAL = pdMS_TO_TICKS(30000);       // 30 seconds
    static const uint32_t BATCH_FLUSH_INTERVAL = pdMS_TO_TICKS(5000); // 5 seconds max batch hold

    while (mRunning) {
        // Wait for SensorData with timeout
        BaseType_t triggered = xSemaphoreTake(mWriteSem, pdMS_TO_TICKS(10));

        if (triggered == pdTRUE && mHasPendingSensorData) {
            mHasPendingSensorData = false;
            appendRecord(&mPendingSensorData);
        }

        // Check for ADC data (using flag instead of semaphore to avoid race condition)
        if (mHasPendingAdcData) {
            mHasPendingAdcData = false;
            
            // Process the pending ADC batch (copy data in task context, not ISR/dispatcher)
            if (mPendingAdcBatch.sampleCount > 0) {
                for (uint16_t i = 0; i < mPendingAdcBatch.sampleCount && mAdcBatchCount < MAX_BATCH_SAMPLES; i++) {
                    uint16_t offset = mAdcBatchCount * AdcDataModel::SAMPLE_SIZE;
                    uint16_t srcOffset = i * AdcDataModel::SAMPLE_SIZE;
                    memcpy(&mAdcBatchBuffer[offset], 
                           &mPendingAdcBatch.sampleBuffer[srcOffset], 
                           AdcDataModel::SAMPLE_SIZE);
                    mAdcBatchCount++;
                    mSamplesInWindow++;
                }
            }
            
            // Flush batch if target reached
            if (mAdcBatchCount >= mAdcBatchTarget) {
                flushAdcBatch();
                lastBatchFlushTick = xTaskGetTickCount();
            }
        }

        uint32_t now = xTaskGetTickCount();

        // Stress test: internal dummy writes at configured rate
        if (mStressTestHz > 0) {
            uint32_t stressInterval = pdMS_TO_TICKS(1000 / mStressTestHz);
            if ((now - lastStressTick) >= stressInterval) {
                appendDummyRecord();
                lastStressTick = now;
            }
        }

        // ADC batch stress test: generate simulated 8ch data at target write rate
        if (mAdcStressTestSps > 0 && mAdcBatchTarget > 0) {
            uint32_t adcInterval = pdMS_TO_TICKS(1000 * (uint32_t)mAdcBatchTarget / mAdcStressTestSps);
            if ((now - lastStressTick) >= adcInterval) {
                appendDummyAdcBatch();
                lastStressTick = now;
            }
        }

        // Periodic status print (every 1 second)
        static uint32_t sLastPrintTick = 0;
        if ((now - sLastPrintTick) >= pdMS_TO_TICKS(1000)) {
            sLastPrintTick = now;
            if (mAdcStressTestSps > 0) {
                printf("[SD] Rec=%lu R=%u/s S=%u/s B=%u E=%lu\n",
                       mRecordCount, mLastRate, mLastSampleRate, mAdcBatchTarget, mConsecErrors);
            } else {
                printf("[SD] Rec=%lu R=%u/s\n", mRecordCount, mLastRate);
            }
        }

        // Periodic batch flush (avoid holding samples too long)
        if (mAdcBatchCount > 0 &&
            (now - lastBatchFlushTick) >= BATCH_FLUSH_INTERVAL) {
            flushAdcBatch();
            lastBatchFlushTick = now;
        }

        // Periodic SD sync
        if ((now - lastSyncTick) >= SYNC_INTERVAL) {
            mFal.sync();
            lastSyncTick = now;
        }

        // Midnight TSDB rotation: rename current → tsdb_YYYYMMDD.db, reinit
        if (SystemClock::getInstance().isSynced()) {
            uint32_t today = SystemClock::dateYYYYMMDD(
                SystemClock::getInstance().now());
            if (lastDay != 0 && today != lastDay) {
                // Flush pending ADC batch
                if (mAdcBatchCount > 0) flushAdcBatch();

                // Deinit current TSDB
                fdb_tsdb_deinit(&mTsdb);
                mFal.sync();

                // Rename current file → tsdb_YYYYMMDD.db
                char dateName[24];
                snprintf(dateName, sizeof(dateName), "tsdb_%08lu.db", (unsigned long)lastDay);
                mFal.reopenTsdb(dateName);

                // Reinit FlashDB on fresh TSDB file (virtual headers → fast)
                uint32_t reinitSec = storage::SdFalAdapter::SECTOR_SIZE;
                fdb_tsdb_control(&mTsdb, FDB_TSDB_CTRL_SET_SEC_SIZE, &reinitSec);
                fdb_tsdb_control(&mTsdb, FDB_TSDB_CTRL_SET_LOCK, (void *)fdbLock);
                fdb_tsdb_control(&mTsdb, FDB_TSDB_CTRL_SET_UNLOCK, (void *)fdbUnlock);

                uint32_t blobMax = 12 + (MAX_BATCH_SAMPLES * AdcDataModel::SAMPLE_SIZE) + 32;
                mFal.setInitScanActive(true);
                fdb_err_t rerr = fdb_tsdb_init(&mTsdb, "sensor", "tsdb", fdbGetTime, blobMax, NULL);
                mFal.setInitScanActive(false);
                if (rerr != FDB_NO_ERR) {
                    printf("[SdStorage] TSDB reinit FAILED: %d\n", rerr);
                } else {
                    mFal.setTsdbMaxSizePtr(&mTsdb.parent.max_size);
                    bool noRollover = false;
                    fdb_tsdb_control(&mTsdb, FDB_TSDB_CTRL_SET_ROLLOVER, &noRollover);
                    printf("[SdStorage] Daily rotation OK → %s\n", dateName);
                }
                sLastTime = 0;
                mNonceCounter = 0;
                mRecordCount = 0;
            }
            lastDay = today;
        }
    }
}

void SdStorageServiceImpl::appendRecord(const SensorDataModel* model) {
    static uint32_t sErrCnt = 0;
    if (!mDbReady) {
        if (++sErrCnt <= 5) printf("[SdStorage] appendRecord: mDbReady=false\n");
        return;
    }

    uint8_t blob[BLOB_SIZE];
    uint8_t* nonce = blob;
    uint8_t* payload = blob + 12;

    uint32_t tick = (uint32_t)xTaskGetTickCount();
    makeNonce(nonce, mNonceCounter, tick);

    serializePayload(model, payload);
    crypto::ChaCha20::crypt(sKey, nonce, 0, payload, PAYLOAD_SIZE);

    struct fdb_blob fblob;
    fdb_err_t err = fdb_tsl_append(&mTsdb, fdb_blob_make(&fblob, blob, BLOB_SIZE));
    if (err == FDB_NO_ERR) {
        mNonceCounter++;
        mRecordCount++;
        mWritesInWindow++;
        updateStats();
    } else {
        if (++sErrCnt <= 20) {
            printf("[SdStorage] fdb_tsl_append error: %d at nonce=%lu\n", err, mNonceCounter);
        }
    }
}

void SdStorageServiceImpl::appendAdcSample(const AdcDataModel* model) {
    if (!mDbReady) return;

    // Add sample to batch buffer
    uint16_t sampleBytes = model->activeChannels * AdcDataModel::BYTES_PER_CHANNEL;
    
    if (mAdcBatchCount == 0) {
        mAdcBatchFirstTime = model->firstTimestamp;
    }

    // Copy sample data to batch buffer
    uint16_t offset = mAdcBatchCount * AdcDataModel::SAMPLE_SIZE;
    if (offset + sampleBytes <= sizeof(mAdcBatchBuffer)) {
        memcpy(&mAdcBatchBuffer[offset], model->sampleBuffer, sampleBytes);
        mAdcBatchCount++;
        mSamplesInWindow++;
    }

    // Flush batch if target reached
    if (mAdcBatchCount >= mAdcBatchTarget) {
        flushAdcBatch();
    }

    updateStats();
}

void SdStorageServiceImpl::flushAdcBatch() {
    if (!mDbReady || mAdcBatchCount == 0) return;

    // Build batch blob: [nonce:12][metadata:16][encrypted_samples:N]
    // Static to avoid ~2.4KB stack usage (task stack is 6KB)
    static uint8_t blob[MAX_BATCH_BLOB_SIZE] __attribute__((aligned(4)));
    uint8_t* nonce = blob;
    uint8_t* metadata = blob + 12;
    uint8_t* samples = blob + 28;

    uint32_t tick = (uint32_t)xTaskGetTickCount();
    makeNonce(nonce, mNonceCounter, tick);

    // Metadata: [version:1][sample_count:2][channel_mask:1][sample_rate:2][timestamp:4][reserved:6]
    metadata[0] = 1;  // Version
    metadata[1] = mAdcBatchCount & 0xFF;
    metadata[2] = (mAdcBatchCount >> 8) & 0xFF;
    metadata[3] = 0xFF;  // All channels active
    metadata[4] = 1000 & 0xFF;  // Sample rate low
    metadata[5] = (1000 >> 8) & 0xFF;  // Sample rate high
    metadata[6] = mAdcBatchFirstTime & 0xFF;
    metadata[7] = (mAdcBatchFirstTime >> 8) & 0xFF;
    metadata[8] = (mAdcBatchFirstTime >> 16) & 0xFF;
    metadata[9] = (mAdcBatchFirstTime >> 24) & 0xFF;
    memset(&metadata[10], 0, 6);  // Reserved

    // Copy samples
    uint16_t samplesLen = mAdcBatchCount * AdcDataModel::SAMPLE_SIZE;
    memcpy(samples, mAdcBatchBuffer, samplesLen);

    // Encrypt metadata + samples
    crypto::ChaCha20::crypt(sKey, nonce, 0, metadata, 16 + samplesLen);

    // Append to TSDB
    struct fdb_blob fblob;
    uint32_t blobLen = 28 + samplesLen;
    fdb_err_t err = fdb_tsl_append(&mTsdb, fdb_blob_make(&fblob, blob, blobLen));

    if (err == FDB_NO_ERR) {
        mNonceCounter++;
        mRecordCount++;
        mWritesInWindow++;
        mConsecErrors = 0;
        updateStats();
    } else {
        mConsecErrors++;
        if (mConsecErrors <= 3 || (mConsecErrors % 100) == 0) {
            printf("[SD] flushAdcBatch ERR=%d consec=%lu nonce=%lu blob=%u\n",
                   err, mConsecErrors, mNonceCounter, (unsigned)blobLen);
        }
    }

    // Clear batch buffer
    mAdcBatchCount = 0;
    memset(mAdcBatchBuffer, 0, sizeof(mAdcBatchBuffer));
}

void SdStorageServiceImpl::updateStats() {
    static volatile uint32_t* const DWT_CYCCNT_PTR = (volatile uint32_t*)0xE0001004;
    uint32_t now = *DWT_CYCCNT_PTR;

    if (mWindowStartTick == 0) {
        mWindowStartTick = now;
    }

    uint32_t elapsed = now - mWindowStartTick;
    uint32_t elapsedMs = elapsed / (SystemCoreClock / 1000);

    if (elapsedMs >= 1000) {
        mLastRate = mWritesInWindow;
        mLastSampleRate = mSamplesInWindow;
        mWritesInWindow = 0;
        mSamplesInWindow = 0;
        mWindowStartTick = now;
    }

    // Publish at most ~4/sec to avoid flooding ObservableDispatcher at high write rates
    static uint8_t sPublishDiv = 0;
    if (++sPublishDiv >= 10) {
        sPublishDiv = 0;
        mStats.recordCount = mRecordCount;
        mStats.writesPerSec = mLastRate;
        mStats.samplesPerSec = mLastSampleRate;
        mStats.batchSize = mAdcBatchTarget;
        mStats.updateTimestamp();
        mStatsObs.publish(&mStats);
    }
}

void SdStorageServiceImpl::makeNonce(uint8_t nonce[12],
                                      uint32_t counter, uint32_t tick) {
    nonce[0]  = (counter >>  0) & 0xFF;
    nonce[1]  = (counter >>  8) & 0xFF;
    nonce[2]  = (counter >> 16) & 0xFF;
    nonce[3]  = (counter >> 24) & 0xFF;
    nonce[4]  = (tick >>  0) & 0xFF;
    nonce[5]  = (tick >>  8) & 0xFF;
    nonce[6]  = (tick >> 16) & 0xFF;
    nonce[7]  = (tick >> 24) & 0xFF;
    nonce[8]  = 0;
    nonce[9]  = 0;
    nonce[10] = 0;
    nonce[11] = 0;
}

void SdStorageServiceImpl::serializePayload(const SensorDataModel* model, uint8_t* buf) {
    uint32_t ts = model->timestamp;
    buf[0] = (ts >>  0) & 0xFF;
    buf[1] = (ts >>  8) & 0xFF;
    buf[2] = (ts >> 16) & 0xFF;
    buf[3] = (ts >> 24) & 0xFF;

    uint32_t temp;
    memcpy(&temp, &model->temperature, 4);
    buf[4] = (temp >>  0) & 0xFF;
    buf[5] = (temp >>  8) & 0xFF;
    buf[6] = (temp >> 16) & 0xFF;
    buf[7] = (temp >> 24) & 0xFF;

    buf[8]  = (uint8_t)(model->accelX & 0xFF);
    buf[9]  = (uint8_t)((model->accelX >> 8) & 0xFF);
    buf[10] = (uint8_t)(model->accelY & 0xFF);
    buf[11] = (uint8_t)((model->accelY >> 8) & 0xFF);
    buf[12] = (uint8_t)(model->accelZ & 0xFF);
    buf[13] = (uint8_t)((model->accelZ >> 8) & 0xFF);
}

void SdStorageServiceImpl::deserializePayload(const uint8_t* buf, SensorDataModel* model) {
    model->timestamp = (uint32_t)buf[0]       | ((uint32_t)buf[1] << 8) |
                       ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

    uint32_t temp = (uint32_t)buf[4]       | ((uint32_t)buf[5] << 8) |
                    ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    memcpy(&model->temperature, &temp, 4);

    model->accelX = (int16_t)((uint16_t)buf[8]  | ((uint16_t)buf[9] << 8));
    model->accelY = (int16_t)((uint16_t)buf[10] | ((uint16_t)buf[11] << 8));
    model->accelZ = (int16_t)((uint16_t)buf[12] | ((uint16_t)buf[13] << 8));
}

void SdStorageServiceImpl::serializeAdcBatch(const uint8_t* samples, uint16_t count, 
                                              uint32_t timestamp, uint8_t* buf, uint32_t& outLen) {
    // Metadata format used in flushAdcBatch
    (void)samples;
    (void)count;
    (void)timestamp;
    (void)buf;
    outLen = 0;
}

// ============================================================
// Daily .enc export
// ============================================================

struct ExportCtx {
    SdStorageServiceImpl* self;
    FIL* fp;
    uint32_t count;
    bool ok;
};

bool SdStorageServiceImpl::exportIterCb(fdb_tsl_t tsl, void* arg) {
    ExportCtx* ctx = static_cast<ExportCtx*>(arg);
    if (tsl->status != FDB_TSL_WRITE) return false;

    uint8_t blob[512];  // Max batch blob size
    struct fdb_blob fblob;
    fdb_blob_make(&fblob, blob, sizeof(blob));
    fblob.saved.addr = tsl->addr.log;
    fblob.saved.len = tsl->log_len;
    
    if (fdb_blob_read((fdb_db_t)&ctx->self->mTsdb, &fblob) != (int)tsl->log_len) {
        return false;
    }

    UINT written;
    FRESULT fr = f_write(ctx->fp, blob, tsl->log_len, &written);
    if (fr != FR_OK || written != tsl->log_len) {
        ctx->ok = false;
        return true;
    }
    ctx->count++;
    return false;
}

bool SdStorageServiceImpl::writeEncFileHeader(FIL* fp, uint32_t recordCount) {
    uint8_t header[8];
    header[0] = 'A';
    header[1] = 'E';
    header[2] = 1;
    header[3] = BLOB_SIZE;
    header[4] = (recordCount >>  0) & 0xFF;
    header[5] = (recordCount >>  8) & 0xFF;
    header[6] = (recordCount >> 16) & 0xFF;
    header[7] = (recordCount >> 24) & 0xFF;

    UINT written;
    FRESULT fr = f_write(fp, header, sizeof(header), &written);
    return (fr == FR_OK && written == sizeof(header));
}

static uint32_t dateToEpoch(uint32_t dateYYYYMMDD) {
    uint32_t y = dateYYYYMMDD / 10000;
    uint32_t m = (dateYYYYMMDD / 100) % 100;
    uint32_t d = dateYYYYMMDD % 100;
    if (m <= 2) { y--; m += 9; } else { m -= 3; }
    uint32_t era = y / 400;
    uint32_t yoe = y - era * 400;
    uint32_t doy = (153 * m + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int32_t days = (int32_t)(era * 146097 + doe) - 719468;
    return (uint32_t)days * 86400;
}

bool SdStorageServiceImpl::exportDailyFile(uint32_t dateYYYYMMDD) {
    if (!mDbReady) return false;

    char fname[16];
    snprintf(fname, sizeof(fname), "%08lu.enc", (unsigned long)dateYYYYMMDD);

    FIL fp;
    FRESULT fr = f_open(&fp, fname, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return false;

    if (!writeEncFileHeader(&fp, 0)) {
        f_close(&fp);
        return false;
    }

    ExportCtx ctx;
    ctx.self = this;
    ctx.fp = &fp;
    ctx.count = 0;
    ctx.ok = true;

    uint32_t dayStart = dateToEpoch(dateYYYYMMDD);
    uint32_t dayEnd = dayStart + 86400 - 1;
    fdb_tsl_iter_by_time(&mTsdb, dayStart, dayEnd, exportIterCb, &ctx);

    if (!ctx.ok || ctx.count == 0) {
        f_close(&fp);
        f_unlink(fname);
        return false;
    }

    f_lseek(&fp, 0);
    writeEncFileHeader(&fp, ctx.count);
    f_close(&fp);

    return true;
}

// ============================================================
// KVDB upload tracking
// ============================================================

bool SdStorageServiceImpl::isDateUploaded(uint32_t dateYYYYMMDD) {
    if (!mDbReady) return false;

    char key[12];
    snprintf(key, sizeof(key), "%08lu", (unsigned long)dateYYYYMMDD);

    uint8_t val = 0;
    struct fdb_blob blob;
    fdb_blob_make(&blob, &val, sizeof(val));
    size_t len = fdb_kv_get_blob(&mKvdb, key, &blob);
    return (len == sizeof(val) && val == 1);
}

bool SdStorageServiceImpl::markDateUploaded(uint32_t dateYYYYMMDD) {
    if (!mDbReady) return false;

    char key[12];
    snprintf(key, sizeof(key), "%08lu", (unsigned long)dateYYYYMMDD);

    uint8_t val = 1;
    struct fdb_blob blob;
    fdb_blob_make(&blob, &val, sizeof(val));
    fdb_err_t err = fdb_kv_set_blob(&mKvdb, key, &blob);
    return (err == FDB_NO_ERR);
}

// ============================================================
// Query helpers
// ============================================================

bool SdStorageServiceImpl::queryIterCb(fdb_tsl_t tsl, void* arg) {
    QueryCtx* ctx = static_cast<QueryCtx*>(arg);
    if (ctx->count >= ctx->maxCount) return true;
    if (tsl->status != FDB_TSL_WRITE) return false;

    uint8_t blob[BLOB_SIZE];
    struct fdb_blob fblob;
    fdb_blob_make(&fblob, blob, BLOB_SIZE);
    fblob.saved.addr = tsl->addr.log;
    fblob.saved.len = tsl->log_len;
    
    // Skip if blob is too large (batch record)
    if (tsl->log_len != BLOB_SIZE) return false;
    
    if (fdb_blob_read((fdb_db_t)&ctx->self->mTsdb, &fblob) != BLOB_SIZE) {
        return false;
    }

    uint8_t* nonce = blob;
    uint8_t* payload = blob + 12;
    crypto::ChaCha20::crypt(sKey, nonce, 0, payload, PAYLOAD_SIZE);

    ctx->self->deserializePayload(payload, &ctx->out[ctx->count]);
    ctx->count++;
    return false;
}

uint16_t SdStorageServiceImpl::queryByDate(uint32_t dateYYYYMMDD,
                                            SensorDataModel* out, uint16_t maxCount) {
    if (!mDbReady) return 0;

    QueryCtx ctx;
    ctx.self = this;
    ctx.out = out;
    ctx.maxCount = maxCount;
    ctx.count = 0;

    uint32_t dayStart = dateToEpoch(dateYYYYMMDD);
    uint32_t dayEnd = dayStart + 86400 - 1;
    fdb_tsl_iter_by_time(&mTsdb, dayStart, dayEnd, queryIterCb, &ctx);

    return ctx.count;
}

uint32_t SdStorageServiceImpl::queryAdcByTimeRange(uint32_t startTime, uint32_t endTime,
                                                    AdcDataModel* out, uint16_t maxBatches) {
    if (!mDbReady) return 0;
    // TODO: Implement ADC batch query and deserialization
    (void)startTime;
    (void)endTime;
    (void)out;
    (void)maxBatches;
    return 0;
}

void SdStorageServiceImpl::publishStats() {
    mStats.updateTimestamp();
    mStatsObs.publish(&mStats);
}

void SdStorageServiceImpl::enableStressTest(uint16_t hz) {
    mStressTestHz = hz;
}

void SdStorageServiceImpl::enableAdcStressTest(uint16_t sps, uint16_t batchSize) {
    mAdcStressTestSps = sps;
    if (batchSize > 0 && batchSize <= MAX_BATCH_SAMPLES) {
        mAdcBatchTarget = batchSize;
    }
    printf("[SdStorage] ADC stress test: %u SPS, batch=%u\n", sps, mAdcBatchTarget);
}

void SdStorageServiceImpl::appendDummyRecord() {
    if (!mDbReady) return;

    SensorDataModel dummy;
    dummy.temperature = 25.0f + (float)(mNonceCounter % 100) * 0.01f;
    dummy.accelX = (int16_t)(mNonceCounter & 0x7FFF);
    dummy.accelY = 0;
    dummy.accelZ = 0;
    dummy.updateTimestamp();
    appendRecord(&dummy);
}

void SdStorageServiceImpl::appendDummyAdcBatch() {
    if (!mDbReady) return;

    // Generate dummy 8-channel ADC samples
    // Each sample: 8 channels × 3 bytes = 24 bytes
    for (uint16_t i = 0; i < mAdcBatchTarget && mAdcBatchCount < MAX_BATCH_SAMPLES; i++) {
        uint16_t offset = mAdcBatchCount * AdcDataModel::SAMPLE_SIZE;
        
        // Generate 8 channels of 24-bit data (3 bytes each)
        for (uint8_t ch = 0; ch < 8; ch++) {
            // Dummy pattern: channel index + sample counter
            uint32_t value = ((mNonceCounter + mAdcBatchCount) * 8 + ch) & 0xFFFFFF;
            mAdcBatchBuffer[offset + ch * 3 + 0] = (value >> 0) & 0xFF;
            mAdcBatchBuffer[offset + ch * 3 + 1] = (value >> 8) & 0xFF;
            mAdcBatchBuffer[offset + ch * 3 + 2] = (value >> 16) & 0xFF;
        }
        
        mAdcBatchCount++;
        mSamplesInWindow++;
    }
    
    // Flush batch immediately
    flushAdcBatch();
}

} // namespace sdstorage
} // namespace arcana
