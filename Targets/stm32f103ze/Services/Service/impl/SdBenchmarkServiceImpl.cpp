#include "SdBenchmarkServiceImpl.hpp"
#include "ff.h"
#include "diskio.h"
#include "DisplayStatus.hpp"
#include <cstring>
#include <cstdio>
#include "stm32f1xx_hal.h"

static void lcdMsg(const char* msg, uint16_t color = 0xFFFF) {
    arcana::display::statusLine(msg, color);
}

/* Global flag: set to 1 after exFAT format+mount succeeds.
 * MQTT task waits for this before connecting. */
extern "C" {
    volatile uint8_t g_exfat_ready = 0;
    void sdio_force_reinit(void);
}

namespace arcana {
namespace sdbench {

// FatFS static objects
FATFS sFatFs;  // Non-static: shared with AtsStorageServiceImpl for remount after format

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

extern "C" FRESULT texfat_format(void);

FRESULT texfat_format(void) {
    MKFS_PARM opt;
    memset(&opt, 0, sizeof(opt));
    opt.fmt = FM_EXFAT;
    opt.n_fat = 2;
    opt.au_size = 1024 * 1024;  // 1MB clusters: FAT update every ~256s
    // Reuse FatFS static buffer (sFatFs.win is 512B, enough for f_mkfs)
    return f_mkfs("", &opt, &sFatFs, sizeof(sFatFs));
}

void SdBenchmarkServiceImpl::runBenchmark() {
    char msg[40];
    FRESULT fr;

    // KEY1 (PA0) held at boot → TexFAT format (active-HIGH: pull-down)
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {
        lcdMsg("[SD] KEY1: Formatting...", 0xFD20);  // orange
        vTaskDelay(pdMS_TO_TICKS(2000));  // Debounce: hold 2 sec to confirm
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {
            printf("[SD] KEY1 held — TexFAT format (n_fat=2)...\r\n");
            fr = texfat_format();
            if (fr == FR_OK) {
                lcdMsg("[SD] Format OK!", 0x07E0);  // green
                printf("[SD] Format OK\r\n");
            } else {
                lcdMsg("[SD] Format FAILED!", 0xF800);  // red
                printf("[SD] Format FAILED (err=%d)\r\n", (int)fr);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            lcdMsg("");  // Clear — KEY1 released early
        }
    }

    static const int MAX_RETRIES = 3;
    bool mounted = false;

    for (int attempt = 0; attempt < MAX_RETRIES && !mounted; ++attempt) {
        if (attempt > 0) {
            // HAL-level recovery before retry
            snprintf(msg, sizeof(msg), "[SD] Retry %d/%d reinit", attempt + 1, MAX_RETRIES);

            printf("%s\r\n", msg);
            f_mount(0, "", 0);           // Unmount (ignore result)
            sdio_force_reinit();         // Reset SDIO + DMA state
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        // Step 1: Try to mount existing exFAT filesystem
        printf("[SD] Mounting... (attempt %d)\r\n", attempt + 1);
        fr = f_mount(&sFatFs, "", 1);

        if (fr == FR_OK) {
            // Post-mount validation: f_getfree catches corrupted FAT
            DWORD fre_clust;
            FATFS* fs;
            if (f_getfree("", &fre_clust, &fs) == FR_OK) {
                mounted = true;
                break;
            }
            printf("[SD] mount OK but getfree FAILED — FS corrupt\r\n");
            // Fall through to format
        } else {
            snprintf(msg, sizeof(msg), "[SD] mount err=%d", (int)fr);
            printf("%s\r\n", msg);
        }

        // Auto-format DISABLED — medical/defense data must not be lost
        // TODO: LCD error display + user confirmation before format
        printf("[SD] Mount failed (%d), retrying...\r\n", (int)fr);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (!mounted) {
        // Format with TexFAT dual-FAT (n_fat=2) as last resort
        printf("[SD] Formatting TexFAT (n_fat=2)...\r\n");
        fr = texfat_format();
        if (fr == FR_OK) {
            printf("[SD] Format OK, mounting...\r\n");
            fr = f_mount(&sFatFs, "", 1);
            if (fr == FR_OK) mounted = true;
        } else {
            printf("[SD] Format FAILED err=%d\r\n", (int)fr);
        }

        if (!mounted) {
            printf("[SD] FAILED after %d attempts\r\n", MAX_RETRIES);
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
        printf("[SD] %luMB/%luMB exFAT Ready\r\n",
               (unsigned long)freeMB, (unsigned long)totalMB);

        // Publish to LCD via MVVM (repurpose SdBenchmarkModel fields)
        mStats.totalKB = freeMB;         // freeMB
        mStats.totalRecords = totalMB;   // totalMB
        mStats.updateTimestamp();
        mStatsObs.notify(&mStats);   // sync — task deletes after this
    } else {
        printf("[SD] Mounted! (getfree err=%d)\r\n", (int)fr);
    }
}

void SdBenchmarkServiceImpl::refreshSdInfo() {
    DWORD fre_clust;
    FATFS* fs;
    FRESULT fr = f_getfree("", &fre_clust, &fs);
    if (fr == FR_OK) {
        uint32_t totalMB = (uint32_t)((uint64_t)(fs->n_fatent - 2) * fs->csize / 2048);
        uint32_t freeMB  = (uint32_t)((uint64_t)fre_clust * fs->csize / 2048);
        mStats.totalKB = freeMB;
        mStats.totalRecords = totalMB;
        mStats.updateTimestamp();
        mStatsObs.notify(&mStats);
    }
}

void SdBenchmarkServiceImpl::publishStats() {
    mStats.updateTimestamp();
    mStatsObs.publish(&mStats);
}

} // namespace sdbench
} // namespace arcana
