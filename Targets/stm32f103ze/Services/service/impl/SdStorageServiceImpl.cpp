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
static fdb_time_t sLastTime = 0;
static fdb_time_t fdbGetTime(void) {
    fdb_time_t now;
    if (SystemClock::getInstance().isSynced()) {
        now = (fdb_time_t)SystemClock::getInstance().now();
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
        storageTask, "SdStore", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 1, mTaskStack, &mTaskBuffer);
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
    
    // Process ADC data immediately (we're in ObservableDispatcher task context)
    // Copy samples from model to batch buffer
    if (model && model->sampleCount > 0) {
        for (uint16_t i = 0; i < model->sampleCount && self->mAdcBatchCount < MAX_BATCH_SAMPLES; i++) {
            uint16_t offset = self->mAdcBatchCount * AdcDataModel::SAMPLE_SIZE;
            uint16_t srcOffset = i * AdcDataModel::SAMPLE_SIZE;
            
            // Copy one sample
            memcpy(&self->mAdcBatchBuffer[offset], 
                   &model->sampleBuffer[srcOffset], 
                   AdcDataModel::SAMPLE_SIZE);
            
            self->mAdcBatchCount++;
            self->mSamplesInWindow++;
        }
        
        // Signal task that ADC data is ready
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

    // Wait for exFAT filesystem to be ready
    int waitCount = 0;
    const int MAX_WAIT = 300;
    while (!g_exfat_ready && self->mRunning && waitCount < MAX_WAIT) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waitCount++;
    }
    if (!self->mRunning) { vTaskDelete(0); return; }

    // Initialize FAL adapter
    if (!self->mFal.init()) {
        vTaskDelete(0);
        return;
    }

    // Initialize FlashDB TSDB
    uint32_t secSize = storage::SdFalAdapter::SECTOR_SIZE;

    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_SEC_SIZE, &secSize);
    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_LOCK, (void *)fdbLock);
    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_UNLOCK, (void *)fdbUnlock);

    // Use larger blob size for batch mode (up to MAX_BATCH_SAMPLES * SAMPLE_SIZE + metadata)
    uint32_t maxBlobSize = 12 + (MAX_BATCH_SAMPLES * AdcDataModel::SAMPLE_SIZE) + 32;
    fdb_err_t err = fdb_tsdb_init(&self->mTsdb, "sensor", "tsdb", fdbGetTime,
                                   maxBlobSize, NULL);
    if (err != FDB_NO_ERR) {
        vTaskDelete(0);
        return;
    }

    // Seed timestamp
    if (self->mTsdb.last_time > 0) {
        sLastTime = self->mTsdb.last_time;
    }

    // Initialize FlashDB KVDB
    fdb_kvdb_control(&self->mKvdb, FDB_KVDB_CTRL_SET_SEC_SIZE, &secSize);
    fdb_kvdb_control(&self->mKvdb, FDB_KVDB_CTRL_SET_LOCK, (void *)fdbLock);
    fdb_kvdb_control(&self->mKvdb, FDB_KVDB_CTRL_SET_UNLOCK, (void *)fdbUnlock);

    err = fdb_kvdb_init(&self->mKvdb, "upload", "kvdb", NULL, NULL);
    if (err != FDB_NO_ERR) {
        fdb_tsdb_deinit(&self->mTsdb);
        vTaskDelete(0);
        return;
    }

    self->mDbReady = true;

    // Get initial nonce counter
    self->mNonceCounter = (uint32_t)fdb_tsl_query_count(
        &self->mTsdb, 0, 0x7FFFFFFF, FDB_TSL_WRITE);

    self->taskLoop();
    vTaskDelete(0);
}

void SdStorageServiceImpl::taskLoop() {
    uint32_t lastDay = 0;
    uint32_t lastSyncTick = xTaskGetTickCount();
    uint32_t lastBatchFlushTick = xTaskGetTickCount();
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
            // Process pending ADC batch if available
            if (mAdcBatchCount >= mAdcBatchTarget) {
                flushAdcBatch();
                lastBatchFlushTick = xTaskGetTickCount();
            }
        }

        // Periodic batch flush (avoid holding samples too long)
        uint32_t now = xTaskGetTickCount();
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

        // Midnight auto-export
        if (SystemClock::getInstance().isSynced()) {
            uint32_t today = SystemClock::dateYYYYMMDD(
                SystemClock::getInstance().now());
            if (lastDay != 0 && today != lastDay) {
                exportDailyFile(lastDay);
            }
            lastDay = today;
        }
    }
}

void SdStorageServiceImpl::appendRecord(const SensorDataModel* model) {
    if (!mDbReady) return;

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
        mWritesInWindow++;
        updateStats();
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
    uint8_t blob[MAX_BATCH_BLOB_SIZE];
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
        mWritesInWindow++;
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

    mStats.recordCount = mNonceCounter;
    mStats.writesPerSec = mLastRate;
    mStats.samplesPerSec = mLastSampleRate;
    mStats.batchSize = mAdcBatchTarget;
    mStats.updateTimestamp();
    mStatsObs.publish(&mStats);
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

} // namespace sdstorage
} // namespace arcana
