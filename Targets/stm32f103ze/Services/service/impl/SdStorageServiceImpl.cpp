#include "stm32f1xx_hal.h"
#include "SdStorageServiceImpl.hpp"
#include "DeviceKey.hpp"
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
// Use raw tick count (ms) so consecutive appends always get unique timestamps.
// Seeded after fdb_tsdb_init to exceed any persisted last_time from previous sessions.
static fdb_time_t sLastTime = 0;
static fdb_time_t fdbGetTime(void) {
    fdb_time_t now = (fdb_time_t)xTaskGetTickCount();
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
    , mPendingData()
    , mWriteSemBuffer()
    , mWriteSem(0)
    , mStatsObs("SdStorage Stats")
    , mStats()
    , mWindowStartTick(0)
    , mWritesInWindow(0)
    , mLastRate(0)
{
    input.SensorData = 0;
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
    return ServiceStatus::OK;
}

void SdStorageServiceImpl::stop() {
    mRunning = false;
    if (mTaskHandle) {
        xSemaphoreGive(mWriteSem);
        vTaskDelay(pdMS_TO_TICKS(50));
        mTaskHandle = 0;
    }
    if (mDbReady) {
        fdb_tsdb_deinit(&mTsdb);
        fdb_kvdb_deinit(&mKvdb);
        mDbReady = false;
    }
}

void SdStorageServiceImpl::onSensorData(SensorDataModel* model, void* context) {
    SdStorageServiceImpl* self = static_cast<SdStorageServiceImpl*>(context);
    self->mPendingData = *model;
    xSemaphoreGive(self->mWriteSem);
}

void SdStorageServiceImpl::storageTask(void* param) {
    SdStorageServiceImpl* self = static_cast<SdStorageServiceImpl*>(param);

    // Wait for exFAT filesystem to be ready
    while (!g_exfat_ready && self->mRunning) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!self->mRunning) { vTaskDelete(0); return; }

    // Initialize FAL adapter (opens/creates partition files)
    if (!self->mFal.init()) {
        vTaskDelete(0);
        return;
    }

    // Initialize FlashDB TSDB
    uint32_t secSize = storage::SdFalAdapter::SECTOR_SIZE;

    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_SEC_SIZE, &secSize);
    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_LOCK, (void *)fdbLock);
    fdb_tsdb_control(&self->mTsdb, FDB_TSDB_CTRL_SET_UNLOCK, (void *)fdbUnlock);

    fdb_err_t err = fdb_tsdb_init(&self->mTsdb, "sensor", "tsdb", fdbGetTime,
                                   BLOB_SIZE, NULL);
    if (err != FDB_NO_ERR) {
        vTaskDelete(0);
        return;
    }

    // Seed timestamp to exceed persisted last_time from previous sessions.
    // Without this, records are dropped after reboot until tick > last_time.
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

    // Get initial nonce counter from TSDB query count
    self->mNonceCounter = (uint32_t)fdb_tsl_query_count(
        &self->mTsdb, 0, 0x7FFFFFFF, FDB_TSL_WRITE);

    self->taskLoop();
    vTaskDelete(0);
}

void SdStorageServiceImpl::taskLoop() {
    while (mRunning) {
        if (xSemaphoreTake(mWriteSem, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (!mRunning) break;
            appendRecord(&mPendingData);
        }
    }
}

void SdStorageServiceImpl::appendRecord(const SensorDataModel* model) {
    if (!mDbReady) return;

    uint8_t blob[BLOB_SIZE];
    uint8_t* nonce = blob;
    uint8_t* payload = blob + 12;

    // Generate unique nonce: [counter:4 LE][tick:4 LE][0x00:4]
    uint32_t tick = (uint32_t)xTaskGetTickCount();
    makeNonce(nonce, mNonceCounter, tick);

    // Serialize sensor data
    serializePayload(model, payload);

    // Encrypt payload in-place
    crypto::ChaCha20::crypt(sKey, nonce, 0, payload, PAYLOAD_SIZE);

    // Append to TSDB
    struct fdb_blob fblob;
    fdb_err_t err = fdb_tsl_append(&mTsdb, fdb_blob_make(&fblob, blob, BLOB_SIZE));
    if (err == FDB_NO_ERR) {
        mNonceCounter++;
        mWritesInWindow++;

        // Update rate (1-second window using DWT cycle counter)
        static volatile uint32_t* const DWT_CYCCNT_PTR = (volatile uint32_t*)0xE0001004;
        uint32_t now = *DWT_CYCCNT_PTR;
        if (mWindowStartTick == 0) {
            mWindowStartTick = now;
        }
        uint32_t elapsed = now - mWindowStartTick;
        uint32_t elapsedMs = elapsed / (SystemCoreClock / 1000);
        if (elapsedMs >= 1000) {
            mLastRate = mWritesInWindow;
            mWritesInWindow = 0;
            mWindowStartTick = now;
        }

        mStats.recordCount = mNonceCounter;
        mStats.writesPerSec = mLastRate;
        mStats.updateTimestamp();
        mStatsObs.publish(&mStats);
    }
}

void SdStorageServiceImpl::makeNonce(uint8_t nonce[12],
                                      uint32_t counter, uint32_t tick) {
    // [counter:4 LE][tick:4 LE][0x00:4]
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
    if (tsl->status != FDB_TSL_WRITE) return false;  // skip non-written

    // Read blob from TSDB
    uint8_t blob[BLOB_SIZE];
    struct fdb_blob fblob;
    fdb_blob_make(&fblob, blob, BLOB_SIZE);
    fblob.saved.addr = tsl->addr.log;
    fblob.saved.len = tsl->log_len;
    if (fdb_blob_read((fdb_db_t)&ctx->self->mTsdb, &fblob) != BLOB_SIZE) {
        return false;  // continue iteration
    }

    // Write blob directly to .enc file (nonce + encrypted payload)
    UINT written;
    FRESULT fr = f_write(ctx->fp, blob, BLOB_SIZE, &written);
    if (fr != FR_OK || written != BLOB_SIZE) {
        ctx->ok = false;
        return true;  // stop iteration
    }
    ctx->count++;
    return false;  // continue
}

bool SdStorageServiceImpl::writeEncFileHeader(FIL* fp, uint32_t recordCount) {
    // Header: [magic:2 "AE"][version:1][record_size:1][record_count:4 LE]
    uint8_t header[8];
    header[0] = 'A';
    header[1] = 'E';
    header[2] = 1;   // version
    header[3] = BLOB_SIZE;  // 26
    header[4] = (recordCount >>  0) & 0xFF;
    header[5] = (recordCount >>  8) & 0xFF;
    header[6] = (recordCount >> 16) & 0xFF;
    header[7] = (recordCount >> 24) & 0xFF;

    UINT written;
    FRESULT fr = f_write(fp, header, sizeof(header), &written);
    return (fr == FR_OK && written == sizeof(header));
}

bool SdStorageServiceImpl::exportDailyFile(uint32_t dateYYYYMMDD) {
    if (!mDbReady) return false;

    // Calculate time range for the day
    uint32_t year  = dateYYYYMMDD / 10000;
    uint32_t month = (dateYYYYMMDD / 100) % 100;
    uint32_t day   = dateYYYYMMDD % 100;

    // Simple epoch-like day start: use date as key, iterate all and filter
    // For TSDB, we use the FlashDB timestamp (seconds since boot).
    // Since we don't have RTC, we export ALL records and name by dateYYYYMMDD.
    // In production with RTC, you'd compute start_of_day/end_of_day epoch seconds.

    // Build filename: "YYYYMMDD.enc"
    char fname[16];
    snprintf(fname, sizeof(fname), "%04lu%02lu%02lu.enc",
             (unsigned long)year, (unsigned long)month, (unsigned long)day);

    // First pass: count records and write to temp file
    FIL fp;
    FRESULT fr = f_open(&fp, fname, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return false;

    // Write placeholder header (will update record_count later)
    if (!writeEncFileHeader(&fp, 0)) {
        f_close(&fp);
        return false;
    }

    // Iterate TSDB and write records
    ExportCtx ctx;
    ctx.self = this;
    ctx.fp = &fp;
    ctx.count = 0;
    ctx.ok = true;

    fdb_tsl_iter(&mTsdb, exportIterCb, &ctx);

    if (!ctx.ok || ctx.count == 0) {
        f_close(&fp);
        f_unlink(fname);
        return false;
    }

    // Update header with actual count
    f_lseek(&fp, 0);
    writeEncFileHeader(&fp, ctx.count);
    f_close(&fp);

    // Mark TSDB records as exported (set USER_STATUS1)
    // This enables future fdb_tsl_clean() to reclaim space
    // (Omitted here to avoid double-iteration; can be done in a separate pass)

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
// LCD date query
// ============================================================

bool SdStorageServiceImpl::queryIterCb(fdb_tsl_t tsl, void* arg) {
    QueryCtx* ctx = static_cast<QueryCtx*>(arg);
    if (ctx->count >= ctx->maxCount) return true;  // stop
    if (tsl->status != FDB_TSL_WRITE) return false;  // skip

    uint8_t blob[BLOB_SIZE];
    struct fdb_blob fblob;
    fdb_blob_make(&fblob, blob, BLOB_SIZE);
    fblob.saved.addr = tsl->addr.log;
    fblob.saved.len = tsl->log_len;
    if (fdb_blob_read((fdb_db_t)&ctx->self->mTsdb, &fblob) != BLOB_SIZE) {
        return false;
    }

    // Decrypt: nonce is first 12 bytes, payload is next 14
    uint8_t* nonce = blob;
    uint8_t* payload = blob + 12;
    crypto::ChaCha20::crypt(sKey, nonce, 0, payload, PAYLOAD_SIZE);

    ctx->self->deserializePayload(payload, &ctx->out[ctx->count]);
    ctx->count++;
    return false;
}

uint16_t SdStorageServiceImpl::queryByDate(uint32_t dateYYYYMMDD,
                                            SensorDataModel* out,
                                            uint16_t maxCount) {
    if (!mDbReady) return 0;

    QueryCtx ctx;
    ctx.self = this;
    ctx.out = out;
    ctx.maxCount = maxCount;
    ctx.count = 0;

    // Iterate all records (no RTC → no time filtering)
    // With RTC, use fdb_tsl_iter_by_time(start_of_day, end_of_day)
    fdb_tsl_iter(&mTsdb, queryIterCb, &ctx);

    return ctx.count;
}

void SdStorageServiceImpl::publishStats() {
    mStats.updateTimestamp();
    mStatsObs.publish(&mStats);
}

} // namespace sdstorage
} // namespace arcana
