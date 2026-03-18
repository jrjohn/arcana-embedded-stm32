#include "stm32f1xx_hal.h"
#include "AtsStorageServiceImpl.hpp"
#include "DeviceKey.hpp"
#include "SystemClock.hpp"
#include "MainView.hpp"
#include "ats/ArcanaTsSchema.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include "ff.h"
#include <cstring>
#include <cstdio>

// Synthetic ECG waveform LUT (one heartbeat, 250 samples @ 250Hz = 1 sec = 60 BPM)
// Values 0-99: 0=top of ECG area, 70=baseline, 99=bottom. Like Lead II.
static const uint8_t ECG_LUT[] = {
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70, // baseline
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70, // baseline
    70,70,69,68,66,64,63,63,64,66, 68,69,70,70,70,70,70,70,70,70, // P wave (small)
    70,70,70,70,70,70,70,70,72,75, 70,50,25, 5, 0, 5,25,50,80,90, // Q-R-S (sharp!)
    85,78,72,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70, // ST segment
    70,69,67,64,61,58,56,55,55,56, 58,61,64,67,69,70,70,70,70,70, // T wave (rounded)
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70, // baseline
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70,
};
static const uint16_t ECG_LUT_LEN = sizeof(ECG_LUT);


extern "C" {
    extern volatile uint8_t g_exfat_ready;
    void sdio_force_reinit(void);
}

namespace arcana {

// Defined in Controller.cpp
extern lcd::MainView* g_mainView;
namespace atsstorage {

// Static storage
uint8_t AtsStorageServiceImpl::sKey[crypto::ChaCha20::KEY_SIZE] = {};
uint8_t AtsStorageServiceImpl::sSlowBuf[ats::BLOCK_SIZE] = {};
uint8_t AtsStorageServiceImpl::sReadCache[ats::BLOCK_SIZE] = {};
uint8_t AtsStorageServiceImpl::sDevSlowBuf[ats::BLOCK_SIZE] = {};

// Time source for ArcanaTS — uses SystemClock epoch or tick fallback
static uint32_t atsGetTime() {
    if (SystemClock::getInstance().isSynced()) {
        return SystemClock::getInstance().now();
    }
    return (uint32_t)xTaskGetTickCount();
}

AtsStorageServiceImpl::AtsStorageServiceImpl()
    : mDb()
    , mFilePort()
    , mMutex()
    , mCipher()
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
    , mRunning(false)
    , mDbReady(false)
    , mDeviceDbReady(false)
    , mPendingData()
    , mWriteSemBuffer()
    , mWriteSem(0)
    , mStatsObs("AtsStorage Stats")
    , mStatsModel()
    , mTotalRecords(0)
    , mWindowStartTick(0)
    , mWritesInWindow(0)
    , mLastRate(0)
{
    input.SensorData = 0;
    output.StatsEvents = &mStatsObs;
}

AtsStorageServiceImpl::~AtsStorageServiceImpl() {
    stop();
}

AtsStorageService& AtsStorageServiceImpl::getInstance() {
    static AtsStorageServiceImpl sInstance;
    return sInstance;
}

ServiceStatus AtsStorageServiceImpl::initHAL() {
    crypto::DeviceKey::deriveKey(sKey);
    return ServiceStatus::OK;
}

ServiceStatus AtsStorageServiceImpl::init() {
    mWriteSem = xSemaphoreCreateBinaryStatic(&mWriteSemBuffer);
    if (!mWriteSem) return ServiceStatus::Error;

    mMutex.init();

    return ServiceStatus::OK;
}

ServiceStatus AtsStorageServiceImpl::start() {
    mRunning = true;
    mTaskHandle = xTaskCreateStatic(
        storageTask, "AtsStore", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 1, mTaskStack, &mTaskBuffer);
    if (!mTaskHandle) return ServiceStatus::Error;

    if (input.SensorData) {
        input.SensorData->subscribe(onSensorData, this);
    }
    return ServiceStatus::OK;
}

void AtsStorageServiceImpl::stop() {
    mRunning = false;
    if (mTaskHandle) {
        xSemaphoreGive(mWriteSem);
        vTaskDelay(pdMS_TO_TICKS(50));
        mTaskHandle = 0;
    }
    if (mDbReady) {
        mDb.close();
        mDbReady = false;
    }
    if (mDeviceDbReady) {
        writeLifecycleEvent(
            static_cast<uint8_t>(ats::LifecycleEventType::PowerOff), 0);
        mDeviceDb.close();
        mDeviceDbReady = false;
    }
}

void AtsStorageServiceImpl::onSensorData(SensorDataModel* model, void* context) {
    AtsStorageServiceImpl* self = static_cast<AtsStorageServiceImpl*>(context);
    self->mPendingData = *model;
    xSemaphoreGive(self->mWriteSem);
}

void AtsStorageServiceImpl::storageTask(void* param) {
    AtsStorageServiceImpl* self = static_cast<AtsStorageServiceImpl*>(param);

    // Wait for exFAT filesystem
    while (!g_exfat_ready && self->mRunning) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!self->mRunning) { vTaskDelete(0); return; }

    // Clean up corrupt files from previous crashes
    f_unlink("sensor_bad.ats");
    printf("[ATS] SD cleanup done\r\n");

    // Open device.ats (permanent lifecycle DB) + restore RTC
    if (self->openDeviceDb()) {
        self->restoreTimeFromDeviceDb();
        self->writeLifecycleEvent(
            static_cast<uint8_t>(ats::LifecycleEventType::PowerOn), 0);
    }

    printf("[ATS] Opening sensor DB...\r\n");
    if (!self->openDailyDb()) {
        printf("[ATS] Open FAILED\r\n");
        vTaskDelete(0);
        return;
    }

    // Write test: verify the DB is actually writable (catches corrupted files)
    {
        uint8_t testRec[RECORD_SIZE] = {};
        uint32_t ts = atsGetTime();
        memcpy(testRec, &ts, 4);
        self->mDb.append(0, testRec);
        uint32_t blkBefore = self->mDb.getStats().blocksWritten;
        self->mDb.flush();
        uint32_t blkAfter = self->mDb.getStats().blocksWritten;
        if (blkAfter <= blkBefore) {
            // Flush failed — may be stale SDIO state from debugger reset
            // Try SDIO reinit + retry before giving up
            printf("[ATS] Write test FAILED, SDIO reinit + retry...\r\n");
            sdio_force_reinit();
            self->mDb.append(0, testRec);
            blkBefore = self->mDb.getStats().blocksWritten;
            self->mDb.flush();
            blkAfter = self->mDb.getStats().blocksWritten;
        }
        if (blkAfter <= blkBefore) {
            // Still failed after reinit — truly corrupted, rename and recreate
            printf("[ATS] Write test FAILED after reinit, renaming...\r\n");
            self->mDb.close();
            self->mDbReady = false;
            f_rename("sensor.ats", "sensor_bad.ats");
            sdio_force_reinit();
            printf("[ATS] Recreating...\r\n");
            if (!self->openDailyDb()) {
                printf("[ATS] Recreate FAILED\r\n");
                vTaskDelete(0);
                return;
            }
            printf("[ATS] Fresh DB created\r\n");
        } else {
            printf("[ATS] Write test OK\r\n");
        }
    }

    printf("[ATS] Ready, 1kHz mode\r\n");
    self->taskLoop();
    vTaskDelete(0);
}

bool AtsStorageServiceImpl::openDailyDb() {
    ats::AtsConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.file = &mFilePort;
    cfg.cipher = &mCipher;
    cfg.mutex = &mMutex;
    cfg.getTime = atsGetTime;
    cfg.key = sKey;
    cfg.deviceUid = (const uint8_t*)UID_BASE;
    cfg.deviceUidSize = 12;
    cfg.overflow = ats::OverflowPolicy::Drop;
    cfg.primaryChannel = 0xFF;  // no primary — all channels use slowBuf
    cfg.primaryBufA = 0;
    cfg.primaryBufB = 0;
    cfg.slowBuf = sSlowBuf;
    cfg.readCache = sReadCache;

    printf("[ATS] db.open...\r\n");
    if (!mDb.open("sensor.ats", cfg)) {
        // Recovery failed — delete corrupt file and retry fresh
        printf("[ATS] db.open failed, deleting corrupt file...\r\n");
        f_unlink("sensor.ats");
        if (!mDb.open("sensor.ats", cfg)) {
            printf("[ATS] db.open FAILED\r\n");
            return false;
        }
    }
    printf("[ATS] db.open OK (started=%d)\r\n", mDb.isOpen() && mDb.getChannelCount() > 0);

    // If recovery loaded channels, skip addChannel/start
    if (!mDb.isReadOnly() && mDb.getChannelCount() == 0) {
        ats::ArcanaTsSchema sensor = ats::ArcanaTsSchema::mpu6050();
        if (!mDb.addChannel(0, sensor)) {
            printf("[ATS] addChannel FAILED\r\n");
            mDb.close();
            return false;
        }

        if (!mDb.start()) {
            printf("[ATS] db.start FAILED\r\n");
            mDb.close();
            return false;
        }
    }

    mDbReady = true;
    mTotalRecords = mDb.getStats().totalRecords;
    printf("[ATS] Ready, resuming at %lu rec\r\n", (unsigned long)mTotalRecords);
    return true;
}

void AtsStorageServiceImpl::rotateDailyDb(uint32_t lastDay) {
    printf("[ATS] Rotating day %lu\r\n", (unsigned long)lastDay);

    // Close current DB (flushes, writes index, syncs)
    mDb.close();
    mDbReady = false;

    // Rename sensor.ats to YYYYMMDD.ats
    char oldName[] = "sensor.ats";
    char newName[16];
    snprintf(newName, sizeof(newName), "%08lu.ats", (unsigned long)lastDay);
    f_rename(oldName, newName);

    // Open fresh DB
    if (openDailyDb()) {
        printf("[ATS] New day DB opened\r\n");
    } else {
        printf("[ATS] New day DB FAILED\r\n");
    }
}

// ---------------------------------------------------------------------------
// device.ats — permanent lifecycle DB
// ---------------------------------------------------------------------------

bool AtsStorageServiceImpl::openDeviceDb() {
    ats::AtsConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.file = &mDeviceFilePort;
    cfg.cipher = &mCipher;
    cfg.mutex = &mMutex;  // shared with sensor DB (same task)
    cfg.getTime = atsGetTime;
    cfg.key = sKey;
    cfg.deviceUid = (const uint8_t*)UID_BASE;
    cfg.deviceUidSize = 12;
    cfg.overflow = ats::OverflowPolicy::Drop;
    cfg.primaryChannel = 0xFF;
    cfg.slowBuf = sDevSlowBuf;
    cfg.readCache = 0;  // device.ats writes rarely, can share slowBuf for reads

    printf("[DEV] Opening device.ats...\r\n");
    if (!mDeviceDb.open("device.ats", cfg)) {
        f_unlink("device.ats");
        if (!mDeviceDb.open("device.ats", cfg)) {
            printf("[DEV] Open FAILED\r\n");
            return false;
        }
    }

    if (mDeviceDb.getChannelCount() == 0) {
        ats::ArcanaTsSchema lc = ats::ArcanaTsSchema::lifecycleEvent();
        if (!mDeviceDb.addChannel(0, lc)) {
            printf("[DEV] addChannel FAILED\r\n");
            mDeviceDb.close();
            return false;
        }
        if (!mDeviceDb.start()) {
            printf("[DEV] start FAILED\r\n");
            mDeviceDb.close();
            return false;
        }
    }

    mDeviceDbReady = true;
    printf("[DEV] device.ats OK, %lu rec\r\n",
           (unsigned long)mDeviceDb.getStats().totalRecords);
    return true;
}

void AtsStorageServiceImpl::restoreTimeFromDeviceDb() {
    if (!mDeviceDbReady) return;
    if (SystemClock::getInstance().isSynced()) return;  // RTC already valid

    // Read latest lifecycle record to get last known timestamp
    // LIFECYCLE record: [ts:4][eventType:1][eventCode:2][reserved:1][param:4] = 12 bytes
    uint8_t buf[12];
    uint16_t n = mDeviceDb.queryLatest(0, buf, 1);
    if (n > 0) {
        uint32_t lastEpoch;
        memcpy(&lastEpoch, buf, 4);
        if (lastEpoch > 1577836800) {  // > 2020-01-01
            SystemClock::getInstance().sync(lastEpoch);
            printf("[DEV] RTC restored: %lu\r\n", (unsigned long)lastEpoch);
        }
    }
}

void AtsStorageServiceImpl::writeLifecycleEvent(uint8_t eventType, uint32_t param) {
    if (!mDeviceDbReady) return;

    // LIFECYCLE schema: ts(U32), evtTyp(U8), evtCod(U16), rsv(U8), param(U32) = 12 bytes
    uint8_t rec[12];
    uint32_t ts = atsGetTime();
    memcpy(rec, &ts, 4);
    rec[4] = eventType;
    rec[5] = 0; rec[6] = 0;  // eventCode = 0
    rec[7] = 0;               // reserved
    memcpy(rec + 8, &param, 4);

    mDeviceDb.append(0, rec);
    mDeviceDb.flush();  // lifecycle events must persist immediately
    printf("[DEV] event type=0x%02X param=%lu\r\n",
           (unsigned)eventType, (unsigned long)param);
}

// ---------------------------------------------------------------------------

void AtsStorageServiceImpl::taskLoop() {
    uint32_t lastDay = 0;
    uint32_t lastReportTick = xTaskGetTickCount();
    TickType_t nextWake = xTaskGetTickCount();
    uint32_t windowOk = 0;
    uint32_t windowFail = 0;
    uint8_t rec[RECORD_SIZE];
    memset(rec, 0, RECORD_SIZE);
    uint32_t ecgPhase = 0;  // LUT index for synthetic ECG

    while (mRunning) {
        // 1kHz pacing — 1 record per ms
        vTaskDelayUntil(&nextWake, 1);

        if (!mDbReady) {
            // DB failed (e.g. rotation error) — retry every 5 seconds
            static uint32_t retryTick = 0;
            uint32_t now = xTaskGetTickCount();
            if (now - retryTick >= pdMS_TO_TICKS(5000)) {
                retryTick = now;
                printf("[ATS] DB not ready, retrying open...\r\n");
                sdio_force_reinit();
                if (openDailyDb()) {
                    printf("[ATS] DB recovered!\r\n");
                } else {
                    printf("[ATS] DB retry FAILED\r\n");
                }
            }
            continue;
        }

        // Synthetic ECG value from LUT
        uint8_t ecgVal = ECG_LUT[ecgPhase % ECG_LUT_LEN];
        ecgPhase++;

        // Push ECG sample to View via queue (250Hz, thread-safe)
        if ((mTotalRecords & 3) == 0 && g_mainView) {
            g_mainView->pushEcgSample(ecgVal);
        }

        // Build synthetic record (will be replaced by real ADS1298 SPI data)
        uint32_t ts = atsGetTime();
        memcpy(rec, &ts, 4);
        float temp = 25.0f + (float)(mTotalRecords % 1000) * 0.01f;
        memcpy(rec + 4, &temp, 4);
        int16_t ax = (int16_t)(50 - (int16_t)ecgVal) * 400;  // ECG as accelerometer value
        memcpy(rec + 8, &ax, 2);
        memcpy(rec + 10, &ax, 2);
        memcpy(rec + 12, &ax, 2);

        if (mDb.append(0, rec)) {
            mTotalRecords++;
            windowOk++;
        } else {
            windowFail++;
        }

        // Report + LCD update every 1 second
        uint32_t now = xTaskGetTickCount();
        if ((now - lastReportTick) >= pdMS_TO_TICKS(1000)) {
            bool flushed = mDb.flush();
            if (!flushed) {
                sdio_force_reinit();
                flushed = mDb.flush();
                if (!flushed) printf("[ATS] flush FAILED (after reinit)\r\n");
            }

            mStatsModel.recordCount = mTotalRecords;
            mStatsModel.writesPerSec = (uint16_t)windowOk;
            mStatsModel.totalKB = (mDb.getStats().blocksWritten + 1) * 4;
            mStatsModel.kbPerSec = (uint16_t)(windowOk * RECORD_SIZE / 1024);
            mStatsModel.updateTimestamp();
            mStatsObs.publish(&mStatsModel);

            printf("[ATS] %lu rec, %lu/s, blk=%lu (%luKB) fail=%lu drop=%lu\r\n",
                   (unsigned long)mTotalRecords, (unsigned long)windowOk,
                   (unsigned long)mDb.getStats().blocksWritten,
                   (unsigned long)mStatsModel.totalKB,
                   (unsigned long)mDb.getStats().blocksFailed,
                   (unsigned long)mDb.getStats().overflowDrops);

            windowOk = 0;
            windowFail = 0;
            lastReportTick = now;

            // Midnight rotation
            if (SystemClock::getInstance().isSynced()) {
                uint32_t today = SystemClock::dateYYYYMMDD(
                    SystemClock::getInstance().now());
                if (lastDay != 0 && today != lastDay) {
                    rotateDailyDb(lastDay);
                }
                lastDay = today;
            }
        }
    }
}

void AtsStorageServiceImpl::appendRecord(const SensorDataModel* model) {
    if (!mDbReady) return;

    uint8_t rec[RECORD_SIZE];
    serializeRecord(model, rec);

    if (mDb.append(0, rec)) {
        mTotalRecords++;
        mWritesInWindow++;

        // Rate tracking (DWT cycle counter, 1-second window)
        static volatile uint32_t* const DWT_CYCCNT = (volatile uint32_t*)0xE0001004;
        uint32_t now = *DWT_CYCCNT;
        if (mWindowStartTick == 0) {
            mWindowStartTick = now;
        }
        uint32_t elapsed = now - mWindowStartTick;
        uint32_t elapsedMs = elapsed / (SystemCoreClock / 1000);
        if (elapsedMs >= 1000) {
            mLastRate = mWritesInWindow;
            printf("[ATS] %lu rec, %u rec/s, blk=%lu\r\n",
                   (unsigned long)mTotalRecords,
                   (unsigned)mLastRate,
                   (unsigned long)mDb.getStats().blocksWritten);
            mWritesInWindow = 0;
            mWindowStartTick = now;
        }

        mStatsModel.recordCount = mTotalRecords;
        mStatsModel.writesPerSec = mLastRate;
        mStatsModel.totalKB = (mDb.getStats().blocksWritten + 1) * 4;
        mStatsModel.kbPerSec = (uint16_t)(mLastRate * RECORD_SIZE / 1024);
        mStatsModel.updateTimestamp();
        mStatsObs.publish(&mStatsModel);
    }
}

void AtsStorageServiceImpl::serializeRecord(const SensorDataModel* model, uint8_t* buf) {
    // ts: U32 (epoch seconds)
    uint32_t ts = atsGetTime();
    memcpy(buf, &ts, 4);

    // temp: F32
    memcpy(buf + 4, &model->temperature, 4);

    // ax, ay, az: I16 each
    memcpy(buf + 8,  &model->accelX, 2);
    memcpy(buf + 10, &model->accelY, 2);
    memcpy(buf + 12, &model->accelZ, 2);
}

uint16_t AtsStorageServiceImpl::queryByDate(uint32_t dateYYYYMMDD,
                                             SensorDataModel* out,
                                             uint16_t maxCount) {
    if (!mDbReady) return 0;

    // Convert YYYYMMDD to epoch range (Howard Hinnant)
    uint32_t y = dateYYYYMMDD / 10000;
    uint32_t m = (dateYYYYMMDD / 100) % 100;
    uint32_t d = dateYYYYMMDD % 100;
    if (m <= 2) { y--; m += 9; } else { m -= 3; }
    uint32_t era = y / 400;
    uint32_t yoe = y - era * 400;
    uint32_t doy = (153 * m + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int32_t days = (int32_t)(era * 146097 + doe) - 719468;
    uint32_t dayStart = (uint32_t)days * 86400;
    uint32_t dayEnd = dayStart + 86400 - 1;

    struct QueryCtx {
        SensorDataModel* out;
        uint16_t maxCount;
        uint16_t count;
    };

    QueryCtx ctx;
    ctx.out = out;
    ctx.maxCount = maxCount;
    ctx.count = 0;

    mDb.queryByTime(0, dayStart, dayEnd,
        [](uint8_t, const uint8_t* rec, uint32_t, void* arg) -> bool {
            QueryCtx* c = static_cast<QueryCtx*>(arg);
            if (c->count >= c->maxCount) return true;

            SensorDataModel& m = c->out[c->count];
            memcpy(&m.timestamp, rec, 4);
            memcpy(&m.temperature, rec + 4, 4);
            memcpy(&m.accelX, rec + 8, 2);
            memcpy(&m.accelY, rec + 10, 2);
            memcpy(&m.accelZ, rec + 12, 2);
            c->count++;
            return false;
        }, &ctx);

    return ctx.count;
}

void AtsStorageServiceImpl::publishStats() {
    mStatsModel.updateTimestamp();
    mStatsObs.publish(&mStatsModel);
}

} // namespace atsstorage
} // namespace arcana
