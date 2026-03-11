#include "SdBenchmarkServiceImpl.hpp"
#include "Ili9341Lcd.hpp"
#include "ff.h"
#include "diskio.h"
#include <cstring>
#include <cstdio>
#include "stm32f1xx_hal.h"

/* Global flag: set to 1 after exFAT format+mount succeeds.
 * MQTT task waits for this before connecting. */
extern "C" { volatile uint8_t g_exfat_ready = 0; }

// LCD debug output for SD status (reuse SD Bench area: y=80..124)
static void sdLcdStatus(const char* msg) {
    arcana::lcd::Ili9341Lcd disp;
    disp.fillRect(20, 80, 200, 16, 0x0000);
    disp.drawString(20, 80, msg, 0xFFFF, 0x0000, 1);
}

static void sdLcdStatus2(const char* msg) {
    arcana::lcd::Ili9341Lcd disp;
    disp.fillRect(20, 100, 200, 10, 0x0000);
    disp.drawString(20, 100, msg, 0x07E0, 0x0000, 1);
}

namespace arcana {
namespace sdbench {

// FatFS static objects
static FATFS sFatFs;

SdBenchmarkServiceImpl::SdBenchmarkServiceImpl()
    : mSd(SdCard::getInstance())
    , mRunning(false)
    , mBlockCount(0)
    , mMkfsBuf{}
    , mStatsObs("SdBench Stats")
    , mStats()
    , mWindowStartTick(0)
    , mBytesInWindow(0)
    , mRecordsInWindow(0)
    , mTotalBytesWritten(0)
    , mTotalRecords(0)
    , mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
{
    output.StatsEvents = &mStatsObs;
}

SdBenchmarkServiceImpl::~SdBenchmarkServiceImpl() {
    stop();
}

SdBenchmarkService& SdBenchmarkServiceImpl::getInstance() {
    static SdBenchmarkServiceImpl sInstance;
    return sInstance;
}

ServiceStatus SdBenchmarkServiceImpl::initHAL() {
    if (!mSd.initHAL()) {
        return ServiceStatus::Error;
    }
    return ServiceStatus::OK;
}

ServiceStatus SdBenchmarkServiceImpl::init() {
    mBlockCount = mSd.getBlockCount();
    if (mBlockCount == 0) return ServiceStatus::Error;
    return ServiceStatus::OK;
}

ServiceStatus SdBenchmarkServiceImpl::start() {
    if (!mSd.isReady()) return ServiceStatus::Error;

    mRunning = true;
    mTaskHandle = xTaskCreateStatic(
        benchmarkTask, "SdBench", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 2, mTaskStack, &mTaskBuffer);
    if (!mTaskHandle) return ServiceStatus::Error;

    return ServiceStatus::OK;
}

void SdBenchmarkServiceImpl::stop() {
    mRunning = false;
    if (mTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        mTaskHandle = 0;
    }
}

void SdBenchmarkServiceImpl::benchmarkTask(void* param) {
    SdBenchmarkServiceImpl* self = static_cast<SdBenchmarkServiceImpl*>(param);
    vTaskDelay(pdMS_TO_TICKS(5000));  // Wait for other services
    self->runBenchmark();
    vTaskDelete(0);
}

void SdBenchmarkServiceImpl::runBenchmark() {
    char msg[40];
    FRESULT fr;
    int retryCount = 0;
    const int MAX_RETRIES = 3;

    // Step 1: Try to mount existing exFAT filesystem (with retries)
    sdLcdStatus("[SD] Mounting...");
    
retry_mount:
    fr = f_mount(&sFatFs, "", 1);

    if (fr != FR_OK) {
        // Mount failed — format as exFAT (first time or corrupted)
        snprintf(msg, sizeof(msg), "[SD] No FS (%d), formatting", (int)fr);
        sdLcdStatus(msg);
        vTaskDelay(pdMS_TO_TICKS(1000));

        MKFS_PARM mkfs_opt;
        memset(&mkfs_opt, 0, sizeof(mkfs_opt));
        mkfs_opt.fmt = FM_EXFAT;
        mkfs_opt.au_size = 0;

        sdLcdStatus("[SD] Formatting exFAT...");
        fr = f_mkfs("", &mkfs_opt, mMkfsBuf, MKFS_BUF_SIZE);
        if (fr != FR_OK) {
            snprintf(msg, sizeof(msg), "[SD] mkfs ERR: %d", (int)fr);
            sdLcdStatus(msg);
            retryCount++;
            if (retryCount < MAX_RETRIES) {
                snprintf(msg, sizeof(msg), "[SD] Retry %d/%d...", retryCount, MAX_RETRIES);
                sdLcdStatus(msg);
                vTaskDelay(pdMS_TO_TICKS(2000));
                goto retry_mount;
            }
            // Even if failed, signal ready to prevent system hang
            sdLcdStatus("[SD] Init Failed!");
            g_exfat_ready = 1;  // Prevent system deadlock
            return;
        }
        sdLcdStatus("[SD] Format OK!");
        vTaskDelay(pdMS_TO_TICKS(500));

        // Mount the freshly formatted filesystem
        fr = f_mount(&sFatFs, "", 1);
        if (fr != FR_OK) {
            snprintf(msg, sizeof(msg), "[SD] mount ERR: %d", (int)fr);
            sdLcdStatus(msg);
            retryCount++;
            if (retryCount < MAX_RETRIES) {
                snprintf(msg, sizeof(msg), "[SD] Retry %d/%d...", retryCount, MAX_RETRIES);
                sdLcdStatus(msg);
                vTaskDelay(pdMS_TO_TICKS(2000));
                goto retry_mount;
            }
            // Even if failed, signal ready to prevent system hang
            sdLcdStatus("[SD] Mount Failed!");
            g_exfat_ready = 1;  // Prevent system deadlock
            return;
        }
    }

    // Filesystem mounted — signal MQTT
    g_exfat_ready = 1;

    // Show card capacity
    DWORD fre_clust;
    FATFS* fs;
    fr = f_getfree("", &fre_clust, &fs);
    if (fr == FR_OK) {
        uint32_t totalMB = (uint32_t)((uint64_t)(fs->n_fatent - 2) * fs->csize / 2048);
        uint32_t freeMB  = (uint32_t)((uint64_t)fre_clust * fs->csize / 2048);
        snprintf(msg, sizeof(msg), "[SD] %luMB/%luMB",
                 (unsigned long)freeMB, (unsigned long)totalMB);
        sdLcdStatus(msg);
        sdLcdStatus2("exFAT Ready");
    } else {
        sdLcdStatus("[SD] Mounted!");
    }
}

void SdBenchmarkServiceImpl::publishStats() {
    mStats.updateTimestamp();
    mStatsObs.publish(&mStats);
}

} // namespace sdbench
} // namespace arcana
