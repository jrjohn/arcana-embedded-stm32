#include "SdFalAdapter.hpp"
#include <fal.h>
#include <cstring>

// FAL structures at file scope (accessed by extern "C" functions below)
static struct fal_flash_dev sFalDev;
static struct fal_partition  sFalParts[2];
static bool sFalInited = false;


namespace arcana {
namespace storage {

const char* SdFalAdapter::TSDB_PATH = "tsdb.fdb";
const char* SdFalAdapter::KVDB_PATH = "kvdb.fdb";

SdFalAdapter::SdFalAdapter()
    : mTsdbFile()
    , mKvdbFile()
    , mInitOk(false)
    , mMutexBuffer()
    , mMutex(0)
{
}

SdFalAdapter::~SdFalAdapter() {}

SdFalAdapter& SdFalAdapter::getInstance() {
    static SdFalAdapter sInstance;
    return sInstance;
}

bool SdFalAdapter::init() {
    if (mInitOk) return true;

    mMutex = xSemaphoreCreateMutexStatic(&mMutexBuffer);
    if (!mMutex) return false;

    // Open or create partition files
    if (!openOrCreate(&mTsdbFile, TSDB_PATH, TSDB_SIZE)) return false;
    if (!openOrCreate(&mKvdbFile, KVDB_PATH, KVDB_SIZE)) return false;

    // Set up FAL flash device
    strncpy(sFalDev.name, "sd_fal", FAL_DEV_NAME_MAX);
    sFalDev.addr = 0;
    sFalDev.len = TSDB_SIZE + KVDB_SIZE;
    sFalDev.blk_size = SECTOR_SIZE;

    // TSDB partition
    strncpy(sFalParts[PART_TSDB].name, "tsdb", FAL_DEV_NAME_MAX);
    strncpy(sFalParts[PART_TSDB].flash_name, "sd_fal", FAL_DEV_NAME_MAX);
    sFalParts[PART_TSDB].offset = 0;
    sFalParts[PART_TSDB].len = TSDB_SIZE;

    // KVDB partition
    strncpy(sFalParts[PART_KVDB].name, "kvdb", FAL_DEV_NAME_MAX);
    strncpy(sFalParts[PART_KVDB].flash_name, "sd_fal", FAL_DEV_NAME_MAX);
    sFalParts[PART_KVDB].offset = TSDB_SIZE;
    sFalParts[PART_KVDB].len = KVDB_SIZE;

    sFalInited = true;
    mInitOk = true;
    return true;
}

bool SdFalAdapter::openOrCreate(FIL* fp, const char* path, uint32_t size) {
    // Retry open in case of transient SDIO errors
    for (int attempt = 0; attempt < 3; attempt++) {
        FRESULT fr = f_open(fp, path, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
        if (fr == FR_OK) {
            if (f_size(fp) >= size) return true;
            f_close(fp);
            break;  // File exists but wrong size — recreate
        }
        if (fr == FR_NO_FILE) break;  // Doesn't exist — create below
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Create and pre-fill with 0xFF (simulates erased flash)
    FRESULT fr = f_open(fp, path, FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
    if (fr != FR_OK) return false;

    uint8_t buf[512];
    memset(buf, 0xFF, sizeof(buf));
    uint32_t remaining = size;
    while (remaining > 0) {
        UINT toWrite = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        UINT written;
        fr = f_write(fp, buf, toWrite, &written);
        if (fr != FR_OK || written != toWrite) {
            f_close(fp);
            return false;
        }
        remaining -= written;
    }
    f_sync(fp);
    f_lseek(fp, 0);
    return true;
}

int SdFalAdapter::read(int partId, uint32_t offset, uint8_t* buf, size_t size) {
    if (!mInitOk) return -1;
    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(500)) != pdTRUE) return -1;

    FIL* fp = (partId == PART_TSDB) ? &mTsdbFile : &mKvdbFile;

    for (int attempt = 0; attempt < 3; attempt++) {
        fp->err = 0;
        FRESULT fr = f_lseek(fp, offset);
        if (fr != FR_OK) { vTaskDelay(1); continue; }

        fp->err = 0;
        UINT bytesRead;
        fr = f_read(fp, buf, size, &bytesRead);
        if (fr == FR_OK && bytesRead == size) {
            xSemaphoreGive(mMutex);
            return 0;
        }
        vTaskDelay(1);
    }

    xSemaphoreGive(mMutex);
    return -1;
}

int SdFalAdapter::write(int partId, uint32_t offset, const uint8_t* buf, size_t size) {
    if (!mInitOk) return -1;
    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(500)) != pdTRUE) return -1;

    FIL* fp = (partId == PART_TSDB) ? &mTsdbFile : &mKvdbFile;

    // Retry loop: transient SDIO errors may cause FatFS disk_read/write to fail.
    // FatFS FIL.err is a sticky flag — must be cleared before every operation.
    for (int attempt = 0; attempt < 3; attempt++) {
        fp->err = 0;
        FRESULT fr = f_lseek(fp, offset);
        if (fr != FR_OK) { vTaskDelay(1); continue; }

        fp->err = 0;
        UINT bytesWritten = 0;
        fr = f_write(fp, buf, size, &bytesWritten);
        if (fr == FR_OK && bytesWritten == size) {
            fp->err = 0;
            f_sync(fp);
            xSemaphoreGive(mMutex);
            return 0;
        }
        vTaskDelay(1);
    }

    xSemaphoreGive(mMutex);
    return -1;
}

int SdFalAdapter::erase(int partId, uint32_t offset, size_t size) {
    if (!mInitOk) return -1;
    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return -1;

    FIL* fp = (partId == PART_TSDB) ? &mTsdbFile : &mKvdbFile;
    fp->err = 0;
    FRESULT fr = f_lseek(fp, offset);
    if (fr != FR_OK) {
        // Retry once
        fp->err = 0;
        vTaskDelay(1);
        fr = f_lseek(fp, offset);
        if (fr != FR_OK) { xSemaphoreGive(mMutex); return -1; }
    }

    uint8_t buf[256];
    memset(buf, 0xFF, sizeof(buf));
    size_t remaining = size;
    while (remaining > 0) {
        UINT toWrite = (remaining > sizeof(buf)) ? sizeof(buf) : (UINT)remaining;
        UINT written;
        fp->err = 0;
        fr = f_write(fp, buf, toWrite, &written);
        if (fr != FR_OK || written != toWrite) {
            xSemaphoreGive(mMutex);
            return -1;
        }
        remaining -= written;
    }
    fp->err = 0;
    f_sync(fp);
    xSemaphoreGive(mMutex);
    return 0;
}

} // namespace storage
} // namespace arcana

// ============================================================
// C-linkage FAL API implementation (called by FlashDB)
// ============================================================
extern "C" {

using Adapter = arcana::storage::SdFalAdapter;

int fal_init(void) {
    return sFalInited ? 0 : -1;
}

const struct fal_partition *fal_partition_find(const char *name) {
    if (!sFalInited) return 0;
    if (strcmp(name, "tsdb") == 0) return &sFalParts[Adapter::PART_TSDB];
    if (strcmp(name, "kvdb") == 0) return &sFalParts[Adapter::PART_KVDB];
    return 0;
}

const struct fal_flash_dev *fal_flash_device_find(const char *name) {
    (void)name;
    if (!sFalInited) return 0;
    return &sFalDev;
}

int fal_partition_read(const struct fal_partition *part, uint32_t addr, uint8_t *buf, size_t size) {
    int partId = (strcmp(part->name, "tsdb") == 0) ? Adapter::PART_TSDB : Adapter::PART_KVDB;
    return Adapter::getInstance().read(partId, addr, buf, size);
}

int fal_partition_write(const struct fal_partition *part, uint32_t addr, const uint8_t *buf, size_t size) {
    int partId = (strcmp(part->name, "tsdb") == 0) ? Adapter::PART_TSDB : Adapter::PART_KVDB;
    return Adapter::getInstance().write(partId, addr, buf, size);
}

int fal_partition_erase(const struct fal_partition *part, uint32_t addr, size_t size) {
    int partId = (strcmp(part->name, "tsdb") == 0) ? Adapter::PART_TSDB : Adapter::PART_KVDB;
    return Adapter::getInstance().erase(partId, addr, size);
}

} // extern "C"
