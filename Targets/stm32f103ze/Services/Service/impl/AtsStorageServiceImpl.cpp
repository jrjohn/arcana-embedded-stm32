#include "stm32f1xx_hal.h"
#include "AtsStorageServiceImpl.hpp"
#include "IoServiceImpl.hpp"
#include "DeviceKey.hpp"
#include "SystemClock.hpp"
#include "MainView.hpp"
#include "ats/ArcanaTsSchema.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include "ff.h"
#include "diskio.h"
#include <cstring>
#include <cstdio>
#include "DisplayStatus.hpp"
#include "SdBenchmarkServiceImpl.hpp"
#include "Log.hpp"
#include "EventCodes.hpp"
#include "SerialAppender.hpp"
#include "AtsAppender.hpp"
#include "DeviceAppender.hpp"
#include "SyslogAppender.hpp"

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
    void sd_card_full_reinit(void);
    void ats_safe_eject(void);
    FRESULT texfat_format(void);
}

// Called from MQTT task when "eject" command received
void ats_safe_eject(void) {
    arcana::atsstorage::AtsStorageServiceImpl& svc =
        static_cast<arcana::atsstorage::AtsStorageServiceImpl&>(
            arcana::atsstorage::AtsStorageServiceImpl::getInstance());
    svc.stop();
}

namespace arcana {
namespace sdbench { extern FATFS sFatFs; }

// Defined in Controller.cpp
extern lcd::MainView* g_mainView;
namespace atsstorage {


// Static storage
uint8_t AtsStorageServiceImpl::sKey[crypto::ChaCha20::KEY_SIZE] = {};
uint8_t AtsStorageServiceImpl::sSlowBuf[ats::BLOCK_SIZE] = {};
uint8_t AtsStorageServiceImpl::sReadCache[ats::BLOCK_SIZE] = {};
uint8_t AtsStorageServiceImpl::sDevSlowBuf[ats::BLOCK_SIZE] = {};
FIL AtsStorageServiceImpl::sSharedFil = {};

// Time source for ArcanaTS — uses SystemClock epoch or tick fallback
static uint32_t atsGetTime() {
    if (SystemClock::getInstance().isSynced()) {
        return SystemClock::getInstance().now();
    }
    return (uint32_t)xTaskGetTickCount();
}


// Logger platform helpers (function pointers for LogConfig)
static void logEnterCritical()  { taskENTER_CRITICAL(); }
static void logExitCritical()   { taskEXIT_CRITICAL(); }
static uint32_t logEnterCriticalISR() { return portSET_INTERRUPT_MASK_FROM_ISR(); }
static void logExitCriticalISR(uint32_t mask) { portCLEAR_INTERRUPT_MASK_FROM_ISR(mask); }
static uint32_t logGetTick() { return (uint32_t)xTaskGetTickCount(); }

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
    , mFormatRequested(false)
    , mUploadPause(false)
    , mUploadRequested(false)
    , mPendingData()
    , mWriteSemBuffer()
    , mWriteSem(0)
    , mStatsObs("AtsStorage Stats")
    , mStatsModel()
    , mTotalRecords(0)
    , mWindowStartTick(0)
    , mWritesInWindow(0)
    , mLastRate(0)
    , mBaselineBlocksFailed(0)
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
        vTaskDelay(pdMS_TO_TICKS(200));  // Wait for storageTask to do cleanup
        mTaskHandle = 0;
    }
}

void AtsStorageServiceImpl::onSensorData(SensorDataModel* model, void* context) {
    AtsStorageServiceImpl* self = static_cast<AtsStorageServiceImpl*>(context);
    self->mPendingData = *model;
    xSemaphoreGive(self->mWriteSem);
}

void AtsStorageServiceImpl::storageTask(void* param) {
    AtsStorageServiceImpl* self = static_cast<AtsStorageServiceImpl*>(param);

    // Initialize Logger with SerialAppender (works before SD is ready)
    static log::SerialAppender sSerialApp;
    static log::AtsAppender    sAtsApp;
    static log::DeviceAppender sDevApp;

    log::LogConfig logCfg;
    memset(&logCfg, 0, sizeof(logCfg));
    logCfg.enterCritical    = logEnterCritical;
    logCfg.exitCritical     = logExitCritical;
    logCfg.enterCriticalISR = logEnterCriticalISR;
    logCfg.exitCriticalISR  = logExitCriticalISR;
    logCfg.getTime          = atsGetTime;
    logCfg.getTick          = logGetTick;
    log::Logger::getInstance().init(logCfg);
    log::Logger::getInstance().addAppender(&sSerialApp);
    log::Logger::getInstance().addAppender(&log::SyslogAppender::getInstance());

    // Wait for exFAT filesystem
    while (!g_exfat_ready && self->mRunning) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!self->mRunning) { vTaskDelete(0); return; }

    // Clean up corrupt files from previous crashes
    f_unlink("sensor_bad.ats");

    // Proactive SDIO reinit — ensures stable reads during DB recovery
    sdio_force_reinit();
    LOG_I(ats::ErrorSource::Sdio, evt::SDIO_REINIT);
    log::Logger::getInstance().drain(8);

    // Open device.ats (permanent lifecycle DB) + restore RTC
    if (self->openDeviceDb()) {
        sDevApp.attach(&self->mDeviceDb);
        log::Logger::getInstance().addAppender(&sDevApp);
        self->restoreTimeFromDeviceDb();
        self->writeLifecycleEvent(
            static_cast<uint8_t>(ats::LifecycleEventType::PowerOn), 0);

        // Load timezone config from device.ats CONFIG (ch1) or tz.cfg fallback
        int16_t tzOff = 0;
        uint8_t tzAuto = 1;
        if (self->loadTzConfig(tzOff, tzAuto)) {
            SystemClock::getInstance().setTzOffset(tzOff);
            LOG_I(ats::ErrorSource::System, 0x0030, (uint32_t)(uint16_t)tzOff);
        }
    }
    log::Logger::getInstance().drain(8);

    LOG_I(ats::ErrorSource::Tsdb, evt::ATS_SENSOR_OPENING);
    if (!self->openDailyDb()) {
        LOG_F(ats::ErrorSource::Tsdb, evt::ATS_DB_OPEN_FAIL);
        log::Logger::getInstance().drain(8);
        // Publish zero stats so LCD shows "0" instead of blank
        self->mStatsModel.recordCount = 0;
        self->mStatsModel.writesPerSec = 0;
        self->mStatsModel.totalKB = 0;
        self->mStatsModel.kbPerSec = 0;
        self->mStatsModel.updateTimestamp();
        self->mStatsObs.publish(&self->mStatsModel);
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
            LOG_W(ats::ErrorSource::Tsdb, evt::ATS_WRITE_TEST_FAIL);
            sdio_force_reinit();
            self->mDb.append(0, testRec);
            blkBefore = self->mDb.getStats().blocksWritten;
            self->mDb.flush();
            blkAfter = self->mDb.getStats().blocksWritten;
        }
        if (blkAfter <= blkBefore) {
            // Write at EOF failed — corrupted cluster chain.
            // Compact: raw-copy valid blocks to a new file (clean clusters).
            // Uses mFilePort for reading (no extra FIL on stack).
            uint32_t validBlocks = self->mDb.getStats().blocksWritten;
            uint32_t validSize = (validBlocks + 1) * ats::BLOCK_SIZE;
            self->mDb.close();
            self->mDbReady = false;
            sdio_force_reinit();

            bool compacted = false;
            if (self->mFilePort.open("sensor.ats", ats::ATS_MODE_READ)) {
                FIL& sDst = sSharedFil;
                if (f_open(&sDst, "sensor_new.ats",
                           FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
                    uint32_t copied = 0;
                    bool ok = true;
                    while (copied < validSize && ok) {
                        int32_t rd = self->mFilePort.read(
                            sSlowBuf, ats::BLOCK_SIZE);
                        if (rd <= 0) { ok = false; break; }
                        UINT wr = 0;
                        if (f_write(&sDst, sSlowBuf, (UINT)rd, &wr)
                            != FR_OK || wr != (UINT)rd)
                            { ok = false; break; }
                        copied += wr;
                    }
                    f_close(&sDst);
                    self->mFilePort.close();
                    if (ok && copied >= validSize) {
                        f_unlink("sensor.ats");
                        f_rename("sensor_new.ats", "sensor.ats");
                        compacted = true;
                        LOG_W(ats::ErrorSource::Tsdb, evt::ATS_RECREATE,
                              validBlocks);
                    } else {
                        f_unlink("sensor_new.ats");
                    }
                } else {
                    self->mFilePort.close();
                }
            }

            if (!compacted) {
                LOG_E(ats::ErrorSource::Tsdb, evt::ATS_WRITE_TEST_FAIL);
                f_rename("sensor.ats", "sensor_bad.ats");
            }

            sdio_force_reinit();
            if (!self->openDailyDb()) {
                LOG_F(ats::ErrorSource::Tsdb, evt::ATS_DB_OPEN_FAIL);
                log::Logger::getInstance().drain(16);
                self->mStatsModel.recordCount = 0;
                self->mStatsModel.writesPerSec = 0;
                self->mStatsModel.totalKB = 0;
                self->mStatsModel.kbPerSec = 0;
                self->mStatsModel.updateTimestamp();
                self->mStatsObs.publish(&self->mStatsModel);
                vTaskDelete(0);
                return;
            }
        }
    }
    log::Logger::getInstance().drain(8);

    // Attach ATS appender now that sensor DB is confirmed writable
    sAtsApp.attach(&self->mDb, 1);
    log::Logger::getInstance().addAppender(&sAtsApp);

    // Log recovery event to both device.ats and sensor.ats
    {
        const ats::StorageStats& st = self->mDb.getStats();
        self->writeRecoveryEvents(
            st.totalRecords,
            (uint16_t)st.recoveryTruncations,
            (uint16_t)(st.blocksFailed - self->mBaselineBlocksFailed));
    }

    // Publish initial stats immediately so LCD shows recovered state
    // (don't wait 1 second for first taskLoop report)
    self->mStatsModel.recordCount = self->mTotalRecords;
    self->mStatsModel.writesPerSec = 0;
    self->mStatsModel.totalKB = (self->mDb.getStats().blocksWritten + 1) * 4;
    self->mStatsModel.kbPerSec = 0;
    self->mStatsModel.updateTimestamp();
    self->mStatsObs.publish(&self->mStatsModel);

    LOG_I(ats::ErrorSource::Tsdb, evt::ATS_READY, self->mTotalRecords);
    log::Logger::getInstance().drain(8);

    for (;;) {
        self->taskLoop();

        // --- Close DBs ---
        LOG_I(ats::ErrorSource::Tsdb, evt::ATS_SHUTDOWN);
        sAtsApp.detach();
        if (self->mDbReady) {
            self->mDb.close();
            self->mDbReady = false;
        }
        if (self->mDeviceDbReady) {
            self->writeLifecycleEvent(
                static_cast<uint8_t>(ats::LifecycleEventType::PowerOff), 0);
            self->mDeviceDb.close();
            self->mDeviceDbReady = false;
        }
        sDevApp.detach();
        log::Logger::getInstance().drain(8);
        // n_fats=2 simple mirror: both FATs always identical, no sync needed

        if (!self->mFormatRequested) {
            // --- Safe eject: unmount, wait for card swap, remount ---
            f_mount(0, "", 0);
            LOG_I(ats::ErrorSource::Tsdb, evt::ATS_SAFE_EJECT);
            display::toast("Safe to remove", 0x7FFFFFFF,
                          (uint32_t)xTaskGetTickCount(),
                          display::colors::WHITE, display::colors::GREEN);

            // Wait for KEY2 press to resume (insert card first)
            bool key2Was = false;
            for (;;) {
                vTaskDelay(pdMS_TO_TICKS(100));
                bool pressed = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13)
                                == GPIO_PIN_RESET);
                if (!pressed) { key2Was = true; continue; }
                if (!key2Was) continue;  // Need release-first
                // KEY2 pressed after being released → remount
                display::toast("Mounting...", 5000, (uint32_t)xTaskGetTickCount(),
                              display::colors::WHITE, 0xFD20);
                LOG_I(ats::ErrorSource::Tsdb, evt::ATS_KEY2_RESUME);
                vTaskDelay(pdMS_TO_TICKS(1000)); // Debounce + card settle
                sd_card_full_reinit();  // Full card re-enumeration after swap
                f_mount(0, "", 0);  // Clear stale mount state
                if (f_mount(&::arcana::sdbench::sFatFs, "", 1) != FR_OK) {
                    display::toast("Mount FAILED!", 3000,
                                  (uint32_t)xTaskGetTickCount(),
                                  display::colors::WHITE, display::colors::RED);
                    LOG_E(ats::ErrorSource::Tsdb, evt::ATS_REMOUNT_FAIL);
                    continue;  // Keep waiting
                }
                break;
            }

            // Reopen DBs
            display::toast("Resumed!", 2000, (uint32_t)xTaskGetTickCount(),
                          display::colors::WHITE, display::colors::GREEN);
            sdio_force_reinit();
            if (self->openDeviceDb()) {
                sDevApp.attach(&self->mDeviceDb);
                self->writeLifecycleEvent(
                    static_cast<uint8_t>(ats::LifecycleEventType::PowerOn), 0);
            }
            if (self->openDailyDb()) {
                sAtsApp.attach(&self->mDb, 1);
            }
            self->mRunning = true;
            LOG_I(ats::ErrorSource::Tsdb, evt::ATS_READY, self->mTotalRecords);
            log::Logger::getInstance().drain(8);
            continue;  // Back to taskLoop
        }

        // --- Runtime format + restart ---
        self->mFormatRequested = false;
        display::toast("Formatting...", 10000, (uint32_t)xTaskGetTickCount(),
                      display::colors::WHITE, 0xFD20);
        f_mount(0, "", 0);

        FRESULT fr = texfat_format();
        if (fr != FR_OK) {
            display::toast("Format FAILED!", 3000,
                          (uint32_t)xTaskGetTickCount(),
                          display::colors::WHITE, display::colors::RED);
            LOG_E(ats::ErrorSource::Tsdb, evt::ATS_FORMAT_FAIL, (uint32_t)fr);
            vTaskDelay(pdMS_TO_TICKS(3000));
            // Retry: reinit SD + try format again next loop
            sdio_force_reinit();
            self->mFormatRequested = true;
            continue;
        }

        // Remount (reuse SdBenchmark's FATFS)
        if (f_mount(&::arcana::sdbench::sFatFs, "", 1) != FR_OK) {
            display::toast("Mount FAILED!", 3000,
                          (uint32_t)xTaskGetTickCount(),
                          display::colors::WHITE, display::colors::RED);
            vTaskDelay(pdMS_TO_TICKS(3000));
            sdio_force_reinit();
            self->mFormatRequested = true;
            continue;
        }
        display::toast("Format OK!", 2000, (uint32_t)xTaskGetTickCount(),
                      display::colors::WHITE, display::colors::GREEN);
        LOG_I(ats::ErrorSource::Tsdb, evt::ATS_FORMAT_OK);

        // Re-publish SD capacity to ViewModel
        static_cast<sdbench::SdBenchmarkServiceImpl&>(
            sdbench::SdBenchmarkServiceImpl::getInstance()).refreshSdInfo();

        vTaskDelay(pdMS_TO_TICKS(1000));

        // Reopen DBs
        if (self->openDeviceDb()) {
            sDevApp.attach(&self->mDeviceDb);
            self->writeLifecycleEvent(
                static_cast<uint8_t>(ats::LifecycleEventType::PowerOn), 0);
        }
        if (self->openDailyDb()) {
            sAtsApp.attach(&self->mDb, 1);
        }

        self->mTotalRecords = 0;
        self->mBaselineBlocksFailed = 0;
        self->mRunning = true;
        LOG_I(ats::ErrorSource::Tsdb, evt::ATS_READY, 0);
        log::Logger::getInstance().drain(8);
        // Loop back to taskLoop
    }

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
    cfg.headerKey = crypto::DeviceKey::getFleetKey();
    cfg.deviceUid = (const uint8_t*)UID_BASE;
    cfg.deviceUidSize = 12;
    cfg.overflow = ats::OverflowPolicy::Drop;
    cfg.primaryChannel = 0xFF;  // no primary — all channels use slowBuf
    cfg.primaryBufA = 0;
    cfg.primaryBufB = 0;
    cfg.slowBuf = sSlowBuf;
    cfg.readCache = sReadCache;

    if (!mDb.open("sensor.ats", cfg)) {
        LOG_W(ats::ErrorSource::Tsdb, evt::ATS_DB_OPEN_FAIL);
        f_unlink("sensor.ats");
        if (!mDb.open("sensor.ats", cfg)) {
            LOG_E(ats::ErrorSource::Tsdb, evt::ATS_DB_OPEN_FAIL);
            return false;
        }
    }
    LOG_I(ats::ErrorSource::Tsdb, evt::ATS_SENSOR_DB_INFO,
          mDb.getStats().totalRecords);

    // If recovery loaded channels, skip addChannel/start
    if (!mDb.isReadOnly() && mDb.getChannelCount() == 0) {
        ats::ArcanaTsSchema sensor = ats::ArcanaTsSchema::mpu6050();
        if (!mDb.addChannel(0, sensor)) {
            LOG_E(ats::ErrorSource::Tsdb, evt::ATS_CHANNEL_FAIL, 0);
            mDb.close();
            return false;
        }

        ats::ArcanaTsSchema errLog = ats::ArcanaTsSchema::errorLog();
        if (!mDb.addChannel(1, errLog)) {
            LOG_E(ats::ErrorSource::Tsdb, evt::ATS_CHANNEL_FAIL, 1);
            mDb.close();
            return false;
        }

        if (!mDb.start()) {
            LOG_E(ats::ErrorSource::Tsdb, evt::ATS_START_FAIL);
            mDb.close();
            return false;
        }
    }

    // OTA upgrade: add missing channels to existing sensor.ats
    upgradeSensorChannels();

    mDbReady = true;
    mTotalRecords = mDb.getStats().totalRecords;
    mBaselineBlocksFailed = mDb.getStats().blocksFailed;
    LOG_I(ats::ErrorSource::Tsdb, evt::ATS_DB_OPEN_OK, mTotalRecords);
    return true;
}

void AtsStorageServiceImpl::rotateDailyDb(uint32_t lastDay) {
    LOG_I(ats::ErrorSource::Tsdb, evt::ATS_ROTATE_OK, lastDay);

    // Close current DB (flushes, writes index, syncs)
    mDb.close();
    mDbReady = false;

    // Rename sensor.ats to YYYYMMDD.ats
    char oldName[] = "sensor.ats";
    char newName[16];
    snprintf(newName, sizeof(newName), "%08lu.ats", (unsigned long)lastDay);
    f_rename(oldName, newName);

    // Open fresh DB
    if (!openDailyDb()) {
        LOG_E(ats::ErrorSource::Tsdb, evt::ATS_ROTATE_FAIL, lastDay);
    }
}

// ---------------------------------------------------------------------------
// device.ats — permanent lifecycle DB
// ---------------------------------------------------------------------------

bool AtsStorageServiceImpl::initDeviceDbChannels() {
    ats::ArcanaTsSchema lc = ats::ArcanaTsSchema::lifecycleEvent();
    if (!mDeviceDb.addChannel(0, lc)) return false;
    ats::ArcanaTsSchema cfgSchema = ats::ArcanaTsSchema::config();
    mDeviceDb.addChannel(1, cfgSchema);
    ats::ArcanaTsSchema creds = ats::ArcanaTsSchema::credentials();
    mDeviceDb.addChannel(2, creds);
    return mDeviceDb.start();
}

bool AtsStorageServiceImpl::openDeviceDbSafe(const ats::AtsConfig& cfg) {
    // Attempt 1: open existing
    if (mDeviceDb.open("device.ats", cfg)) {
        // Live upgrade: add missing channels (OTA)
        if (mDeviceDb.getChannelCount() > 0 && mDeviceDb.getChannelCount() < 3) {
            ats::ArcanaTsSchema creds = ats::ArcanaTsSchema::credentials();
            if (mDeviceDb.addChannelLive(2, creds)) {
                printf("[ATS] device.ats upgraded: added CREDS ch2\r\n");
            }
        }
        return true;
    }

    // Attempt 2: existing file corrupt → preserve + recreate
    printf("[ATS] device.ats corrupt — preserving as device_old.ats\r\n");
    f_unlink("device_old.ats");
    f_rename("device.ats", "device_old.ats");
    if (mDeviceDb.open("device.ats", cfg)) {
        if (initDeviceDbChannels()) return true;
        mDeviceDb.close();
    }

    // Attempt 3: give up
    return false;
}

bool AtsStorageServiceImpl::openDeviceDb() {
    ats::AtsConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.file = &mDeviceFilePort;
    cfg.cipher = &mCipher;
    cfg.mutex = &mMutex;  // shared with sensor DB (same task)
    cfg.getTime = atsGetTime;
    cfg.key = sKey;
    cfg.headerKey = crypto::DeviceKey::getFleetKey();
    cfg.deviceUid = (const uint8_t*)UID_BASE;
    cfg.deviceUidSize = 12;
    cfg.overflow = ats::OverflowPolicy::Drop;
    cfg.primaryChannel = 0xFF;
    cfg.slowBuf = sDevSlowBuf;
    cfg.readCache = sReadCache;

    f_unlink("device.ats.bak");  // clean up old migration artifact

    if (!openDeviceDbSafe(cfg)) {
        LOG_E(ats::ErrorSource::Tsdb, evt::ATS_DB_OPEN_FAIL);
        printf("[ATS] device.ats unavailable — running degraded\r\n");
        return true;  // never block boot
    }

    LOG_I(ats::ErrorSource::Tsdb, evt::ATS_DEVICE_DB_INFO,
          mDeviceDb.getStats().totalRecords);

    // Write test
    {
        uint8_t testRec[12] = {};
        uint32_t ts = atsGetTime();
        memcpy(testRec, &ts, 4);
        testRec[4] = 0xFF;
        mDeviceDb.append(0, testRec);
        uint32_t blkBefore = mDeviceDb.getStats().blocksWritten;
        mDeviceDb.flush();
        if (mDeviceDb.getStats().blocksWritten <= blkBefore) {
            LOG_W(ats::ErrorSource::Sdio, evt::SDIO_REINIT);
            sdio_force_reinit();
            mDeviceDb.append(0, testRec);
            mDeviceDb.flush();
        }
    }

    mDeviceDbReady = true;
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
            LOG_I(ats::ErrorSource::System, evt::SYS_BOOT_RTC_RESTORE, lastEpoch);
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

    // Map LifecycleEventType → event code for Logger
    uint16_t logCode = evt::ATS_LIFECYCLE_ON;
    switch (eventType) {
        case 0x01: logCode = evt::ATS_LIFECYCLE_ON;    break;
        case 0x02: logCode = evt::ATS_LIFECYCLE_OFF;   break;
        case 0x03: logCode = evt::ATS_LIFECYCLE_RECOV; break;
        case 0x10: logCode = evt::ATS_LIFECYCLE_FWUPD; break;
        default:   logCode = evt::ATS_LIFECYCLE_ON;    break;
    }
    LOG_W(ats::ErrorSource::Tsdb, logCode, param);
}

void AtsStorageServiceImpl::writeRecoveryEvents(
        uint32_t recoveredRec, uint16_t truncations, uint16_t skippedBlocks) {
    // 1. device.ats — LIFECYCLE Recovery event
    //    evtTyp=Recovery, evtCod=truncations, param=recoveredRec
    if (mDeviceDbReady) {
        uint8_t rec[12];
        uint32_t ts = atsGetTime();
        memcpy(rec, &ts, 4);
        rec[4] = static_cast<uint8_t>(ats::LifecycleEventType::Recovery);
        uint16_t code = truncations;
        memcpy(rec + 5, &code, 2);
        rec[7] = (uint8_t)skippedBlocks;
        memcpy(rec + 8, &recoveredRec, 4);
        mDeviceDb.append(0, rec);
        mDeviceDb.flush();
        LOG_W(ats::ErrorSource::Tsdb, evt::ATS_RECOVERY_OK, recoveredRec);
    }

    // 2. sensor.ats — ERROR_LOG ch1 (if channel exists)
    //    sev=INFO(1), src=ATS(0x10), errCod=truncations, param=recoveredRec
    if (mDbReady && mDb.getChannelCount() > 1) {
        uint8_t rec[12];
        uint32_t ts = atsGetTime();
        memcpy(rec, &ts, 4);
        rec[4] = 0x01;   // severity: INFO
        rec[5] = 0x10;   // source: ATS recovery
        uint16_t code = truncations;
        memcpy(rec + 6, &code, 2);
        memcpy(rec + 8, &recoveredRec, 4);
        mDb.append(1, rec);
        // flush happens in taskLoop's first 1-second window
    }
}

// ---------------------------------------------------------------------------
// Upload support: file enumeration + upload tracking
// ---------------------------------------------------------------------------

uint8_t AtsStorageServiceImpl::listPendingUploads(PendingFile* out, uint8_t maxCount) {
    uint8_t count = 0;

    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, "") != FR_OK) return 0;

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] && count < maxCount) {
        // Match YYYYMMDD.ats (8 digits + ".ats" = 12 chars)
        const char* name = fno.fname;
        uint16_t len = strlen(name);
        if (len != 12) continue;
        if (name[8] != '.' || name[9] != 'a' || name[10] != 't' || name[11] != 's') continue;

        // Skip sensor.ats and device.ats
        if (strcmp(name, "sensor.ats") == 0 || strcmp(name, "device.ats") == 0) continue;

        // Parse date
        uint32_t date = 0;
        bool valid = true;
        for (int i = 0; i < 8; i++) {
            if (name[i] < '0' || name[i] > '9') { valid = false; break; }
            date = date * 10 + (name[i] - '0');
        }
        if (!valid || date < 20200101) continue;

        // Check if already uploaded
        if (isDateUploaded(date)) continue;

        // Add to list
        strncpy(out[count].name, name, 15);
        out[count].name[15] = '\0';
        out[count].size = (uint32_t)fno.fsize;
        out[count].date = date;
        count++;
    }
    f_closedir(&dir);
    return count;
}

bool AtsStorageServiceImpl::isDateUploaded(uint32_t dateYYYYMMDD) {
    if (!mDeviceDbReady) return false;

    // Query latest 64 LIFECYCLE records using shared read cache
    // Each LIFECYCLE record = 12 bytes: [ts:4][evtType:1][evtCode:2][rsv:1][param:4]
    static const uint16_t REC_SIZE = 12;
    static const uint16_t SCAN_COUNT = ats::BLOCK_SIZE / REC_SIZE;  // ~341 records fit in 4KB
    uint8_t* buf = sReadCache;  // reuse 4KB read cache (same task, sequential)

    uint16_t n = mDeviceDb.queryLatest(0, buf, SCAN_COUNT);
    for (uint16_t i = 0; i < n; i++) {
        uint8_t* rec = buf + i * REC_SIZE;
        uint8_t evtType = rec[4];
        uint32_t param;
        memcpy(&param, rec + 8, 4);

        if (evtType == static_cast<uint8_t>(ats::LifecycleEventType::UploadDone) &&
            param == dateYYYYMMDD) {
            return true;
        }
    }
    return false;
}

void AtsStorageServiceImpl::markUploaded(uint32_t dateYYYYMMDD) {
    writeLifecycleEvent(
        static_cast<uint8_t>(ats::LifecycleEventType::UploadDone),
        dateYYYYMMDD);
}

// ---------------------------------------------------------------------------

bool AtsStorageServiceImpl::loadTzConfig(int16_t& offsetMin, uint8_t& autoCheck) {
    // Try device.ats ch1 (CONFIG) first
    if (mDeviceDbReady && mDeviceDb.getChannelCount() > 1) {
        // CONFIG record: 17 bytes — tzOff at offset 14 (I16), tzAuto at offset 16 (U8)
        uint8_t buf[17];
        uint16_t n = mDeviceDb.queryLatest(1, buf, 1);
        if (n > 0) {
            memcpy(&offsetMin, buf + 14, 2);
            autoCheck = buf[16];
            return true;
        }
    }

    // No ch1 CONFIG → first boot or old device.ats, will auto-detect via WiFi
    return false;
}

bool AtsStorageServiceImpl::saveTzConfig(int16_t offsetMin, uint8_t autoCheck) {
    // Write to device.ats ch1 (CONFIG) if available
    if (mDeviceDbReady && mDeviceDb.getChannelCount() > 1) {
        uint8_t rec[17];
        memset(rec, 0, sizeof(rec));
        uint32_t ts = atsGetTime();
        memcpy(rec, &ts, 4);
        // tzOff at offset 14, tzAuto at offset 16
        memcpy(rec + 14, &offsetMin, 2);
        rec[16] = autoCheck;
        mDeviceDb.append(1, rec);
        mDeviceDb.flush();
        return true;
    }

    // No ch1 CONFIG on old device.ats — timezone will be re-detected next boot
    return false;
}

// ---------------------------------------------------------------------------
// Credentials persistence — device.ats ch2 (CREDS)
// Record: [ts:4][data:232] where data = [nonce:12][encrypted_creds:220]
// ---------------------------------------------------------------------------

bool AtsStorageServiceImpl::loadCredentials(uint8_t* outBuf, uint16_t bufSize,
                                             uint16_t& outLen) {
    outLen = 0;
    if (!mDeviceDbReady || mDeviceDb.getChannelCount() < 3) return false;

    // CREDS record = 236 bytes (4 ts + 232 data)
    uint8_t rec[236];
    uint16_t n = mDeviceDb.queryLatest(2, rec, 1);
    if (n == 0) return false;

    // data starts at offset 4 (after ts)
    uint16_t dataLen = 232;
    if (dataLen > bufSize) dataLen = bufSize;
    memcpy(outBuf, rec + 4, dataLen);
    outLen = dataLen;
    return true;
}

bool AtsStorageServiceImpl::saveCredentials(const uint8_t* data, uint16_t len) {
    printf("[ATS] saveCreds: ready=%d ch=%d len=%u\r\n",
           mDeviceDbReady, mDeviceDb.getChannelCount(), len);
    if (!mDeviceDbReady || mDeviceDb.getChannelCount() < 3) return false;
    if (len > 232) return false;

    uint8_t rec[236];
    memset(rec, 0, sizeof(rec));
    uint32_t ts = atsGetTime();
    memcpy(rec, &ts, 4);
    memcpy(rec + 4, data, len);

    printf("[ATS] saveCreds: append...\r\n");
    bool appOk = mDeviceDb.append(2, rec);
    printf("[ATS] saveCreds: append=%d, flush...\r\n", appOk);
    mDeviceDb.flush();
    printf("[ATS] saveCreds: done\r\n");
    return true;
}

// ---------------------------------------------------------------------------
// Schema upgrade after OTA — add missing channels to existing .ats
// ---------------------------------------------------------------------------

void AtsStorageServiceImpl::upgradeSensorChannels() {
    if (mDb.isReadOnly()) return;
    // Future channels defined here. addChannelLive() is a no-op if already active.
    // Example: if OTA adds USER_ACTION channel to sensor DB:
    // ats::ArcanaTsSchema ua = ats::ArcanaTsSchema::userAction();
    // mDb.addChannelLive(2, ua);
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
    uint8_t key1Hold = 0;   // KEY1 hold counter (seconds)
    uint8_t key2Hold = 0;   // KEY2 state (0=released, 1=pressed)
    bool key2Seen = false;  // KEY2 must be seen released (HIGH) before first detect
    uint32_t key2PressStart = 0;  // tick when KEY2 was first pressed

    bool provisionToastShown = false;

    while (mRunning) {
        // Cooperative pause for upload (yield cleanly after finishing write)
        while (mUploadPause && mRunning) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // 1kHz pacing — 1 record per ms
        vTaskDelayUntil(&nextWake, 1);

        // Drain log events (1 per ms = up to 1000 events/s)
        if (log::Logger::getInstance().pending() > 0) {
            log::Logger::getInstance().drain(1);
        }

        if (!mDbReady) {
            // DB failed (e.g. rotation error) — retry every 5 seconds
            static uint32_t retryTick = 0;
            uint32_t now = xTaskGetTickCount();
            if (now - retryTick >= pdMS_TO_TICKS(5000)) {
                retryTick = now;
                LOG_W(ats::ErrorSource::Tsdb, evt::ATS_DB_RETRY_FAIL);
                sdio_force_reinit();
                if (openDailyDb()) {
                    LOG_I(ats::ErrorSource::Tsdb, evt::ATS_DB_RECOVERED);
                } else {
                    LOG_E(ats::ErrorSource::Tsdb, evt::ATS_DB_RETRY_FAIL);
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
                if (!flushed) LOG_E(ats::ErrorSource::Tsdb, evt::ATS_FLUSH_FAIL,
                                    mDb.getStats().blocksFailed);
            }

            mStatsModel.recordCount = mTotalRecords;
            mStatsModel.writesPerSec = (uint16_t)windowOk;
            mStatsModel.totalKB = (mDb.getStats().blocksWritten + 1) * 4;
            mStatsModel.kbPerSec = (uint16_t)(windowOk * RECORD_SIZE / 1024);
            mStatsModel.updateTimestamp();
            mStatsObs.publish(&mStatsModel);

            uint32_t sessionFail = mDb.getStats().blocksFailed - mBaselineBlocksFailed;
            printf("[ATS] %lu rec, %lu/s, blk=%lu (%luKB) fail=%lu drop=%lu\r\n",
                   (unsigned long)mTotalRecords, (unsigned long)windowOk,
                   (unsigned long)mDb.getStats().blocksWritten,
                   (unsigned long)mStatsModel.totalKB,
                   (unsigned long)sessionFail,
                   (unsigned long)mDb.getStats().overflowDrops);

            // Send stats to syslog (picked up by MQTT task)
            log::SyslogAppender::getInstance().sendStats(
                mTotalRecords, (uint16_t)windowOk, mStatsModel.totalKB,
                atsGetTime());

            // One-time warning after LCD is guaranteed ready
            if (!provisionToastShown) {
                provisionToastShown = true;
                if (!crypto::DeviceKey::isProvisioned()) {
                    LOG_W(ats::ErrorSource::Crypto, 0xFF00);  // debug: not provisioned
                    display::toast("NOT PROVISIONED", 5000,
                                   now, display::colors::WHITE, display::colors::RED);
                }
            }

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

            // Size guard removed — IFilePort now uses uint64_t, no 4GB limit

            // KEY1/KEY2 handled by IoServiceImpl (independent task)
            {
                auto& ioSvc = io::IoServiceImpl::getInstance();
                if (ioSvc.isFormatRequested()) {
                    ioSvc.clearFormatRequest();
                    LOG_W(ats::ErrorSource::Tsdb, evt::ATS_KEY1_FORMAT);
                    mRunning = false;
                    mFormatRequested = true;
                }
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
