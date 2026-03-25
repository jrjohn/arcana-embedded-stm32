#pragma once

#include "AtsStorageService.hpp"
#include "ats/ArcanaTsDb.hpp"
#include "FatFsFilePort.hpp"
#include "FreeRtosMutex.hpp"
#include "ChaCha20Cipher.hpp"
#include "ChaCha20.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

namespace arcana {
namespace atsstorage {

/**
 * ArcanaTS-based storage service.
 *
 * Replaces FlashDB SdStorageService with ArcanaTS v2:
 * - Single .ats file per day on SD card (exFAT)
 * - Multi-channel support (currently: MPU6050 sensor data)
 * - Block I/O: 4KB writes, 290 records/block
 * - ChaCha20 encryption, CRC-32 integrity
 * - Daily midnight rotation
 */
class AtsStorageServiceImpl : public AtsStorageService {
public:
    static AtsStorageService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    uint16_t queryByDate(uint32_t dateYYYYMMDD,
                         SensorDataModel* out, uint16_t maxCount) override;

    /** Upload support: list pending .ats files and track upload status */
    struct PendingFile {
        char name[16];      // "YYYYMMDD.ats"
        uint32_t size;      // file size in bytes
        uint32_t date;      // YYYYMMDD as uint32
    };
    static const uint8_t MAX_PENDING = 8;

    /** Scan SD for YYYYMMDD.ats files not yet uploaded. Returns count. */
    uint8_t listPendingUploads(PendingFile* out, uint8_t maxCount);

    /** Check if a date has been uploaded (search device.ats LIFECYCLE). */
    bool isDateUploaded(uint32_t dateYYYYMMDD);

    /** Mark date as uploaded in device.ats. */
    void markUploaded(uint32_t dateYYYYMMDD);

    /** Shared 4KB read cache — for upload file streaming */
    static uint8_t* getReadCache() { return sReadCache; }

    /** True when DB is open and recording (boot complete) */
    bool isReady() const { return mDbReady; }

    /** Cooperative pause/resume — ATS task yields cleanly between writes */
    void pauseRecording()  { mUploadPause = true; }
    void resumeRecording() { mUploadPause = false; }
    bool isPaused() const  { return mUploadPause; }

    /** Shared FIL for sequential operations (compact at boot, upload at runtime) */
    static FIL sSharedFil;

private:
    AtsStorageServiceImpl();
    ~AtsStorageServiceImpl();
    AtsStorageServiceImpl(const AtsStorageServiceImpl&);
    AtsStorageServiceImpl& operator=(const AtsStorageServiceImpl&);

    // Observer callback
    static void onSensorData(SensorDataModel* model, void* context);

    // Dedicated task
    static void storageTask(void* param);
    void taskLoop();
    void appendRecord(const SensorDataModel* model);

    // Daily rotation
    bool openDailyDb();
    void rotateDailyDb(uint32_t lastDay);

    // Record serialization (matches MPU6050 schema: ts,temp,ax,ay,az = 14 bytes)
    static const uint16_t RECORD_SIZE = 14;
    void serializeRecord(const SensorDataModel* model, uint8_t* buf);

    void publishStats();

    // ArcanaTS sensor DB (daily rotation)
    ats::ArcanaTsDb mDb;
    ats::FatFsFilePort mFilePort;
    ats::FreeRtosMutex mMutex;
    ats::ChaCha20Cipher mCipher;

    // ArcanaTS device DB (permanent, never rotated)
    ats::ArcanaTsDb mDeviceDb;
    ats::FatFsFilePort mDeviceFilePort;
    bool openDeviceDb();
    void restoreTimeFromDeviceDb();
    void writeLifecycleEvent(uint8_t eventType, uint32_t param);
    void writeRecoveryEvents(uint32_t recoveredRec, uint16_t truncations,
                             uint16_t skippedBlocks);

public:
    // Timezone config — stored in device.ats CONFIG channel (ch1) or tz.cfg fallback
    bool loadTzConfig(int16_t& offsetMin, uint8_t& autoCheck);
    bool saveTzConfig(int16_t offsetMin, uint8_t autoCheck);

private:

    // Buffers (static, no heap)
    static uint8_t sSlowBuf[ats::BLOCK_SIZE];
    static uint8_t sReadCache[ats::BLOCK_SIZE];
    static uint8_t sDevSlowBuf[ats::BLOCK_SIZE];

    // Per-device encryption key
    static uint8_t sKey[crypto::ChaCha20::KEY_SIZE];

    // Dedicated task
    static const uint16_t TASK_STACK_SIZE = 1024;
    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;
    bool mDbReady;
    bool mDeviceDbReady;
    volatile bool mFormatRequested;
    volatile bool mUploadPause;

    // Pending write data
    SensorDataModel mPendingData;
    StaticSemaphore_t mWriteSemBuffer;
    SemaphoreHandle_t mWriteSem;

    // Stats
    Observable<StorageStatsModel> mStatsObs;
    StorageStatsModel mStatsModel;
    uint32_t mTotalRecords;
    uint32_t mWindowStartTick;
    uint16_t mWritesInWindow;
    uint16_t mLastRate;
    uint32_t mBaselineBlocksFailed;  // historical fail count at boot
};

} // namespace atsstorage
} // namespace arcana
