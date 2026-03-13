#include "SdFalAdapter.hpp"
#include <fal.h>
#include <cstring>
#include <cstdio>
#include "FreeRTOS.h"
#include "task.h"

// FAL structures at file scope (accessed by extern "C" functions below)
static struct fal_flash_dev sFalDev;
static struct fal_partition  sFalParts[2];
static bool sFalInited = false;

// Fake sector headers: status=STORE_EMPTY(0x7F) + padding(0xFF×3) + magic(4 bytes)
// FlashDB sees these as "formatted empty" sectors → no format_all during init
static const uint8_t sTsdbFakeHdr[8] = {0x7F, 0xFF, 0xFF, 0xFF, 0x54, 0x53, 0x4C, 0x30};
static const uint8_t sKvdbFakeHdr[8] = {0x7F, 0xFF, 0xFF, 0xFF, 0x46, 0x44, 0x42, 0x30};


namespace arcana {
namespace storage {

const char* SdFalAdapter::TSDB_PATH = "tsdb.fdb";
const char* SdFalAdapter::KVDB_PATH = "kvdb.fdb";

SdFalAdapter::SdFalAdapter()
    : mTsdbFile()
    , mKvdbFile()
    , mInitOk(false)
    , mInitScanActive(false)
    , mKvdbBitmap{}
    , mMutexBuffer()
    , mMutex(0)
{
}

SdFalAdapter::~SdFalAdapter() {}

SdFalAdapter& SdFalAdapter::getInstance() {
    static SdFalAdapter sInstance;
    return sInstance;
}

// Uncomment to delete .fdb files on boot (clean start for testing)
#define DELETE_FDB_ON_BOOT

bool SdFalAdapter::init() {
    if (mInitOk) return true;

    mMutex = xSemaphoreCreateMutexStatic(&mMutexBuffer);
    if (!mMutex) return false;

#ifdef DELETE_FDB_ON_BOOT
    f_unlink(TSDB_PATH);
    f_unlink(KVDB_PATH);
    printf("[SdFal] Deleted FDB files on boot\n");
#endif

    // Open or create partition files
    // TSDB: auto-grow (size=0, no f_expand), KVDB: fixed size (f_expand)
    if (!openOrCreate(&mTsdbFile, TSDB_PATH, 0, NULL, 0)) return false;

    // Debug: dump first 8 bytes of TSDB file to verify sector header layout
    if (f_size(&mTsdbFile) >= 8) {
        uint8_t hdr[8];
        UINT br;
        mTsdbFile.err = 0;
        f_lseek(&mTsdbFile, 0);
        f_read(&mTsdbFile, hdr, 8, &br);
        printf("[SdFal] Sector0 bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5], hdr[6], hdr[7]);
    }

    if (!openOrCreate(&mKvdbFile, KVDB_PATH, KVDB_SIZE, mKvdbBitmap, KVDB_BITMAP_BYTES)) return false;

    // Dynamic TSDB partition size: actual file + headroom
    // Must be generous — FlashDB checks sector availability internally before
    // calling FAL read/write, so the partition must always exceed actual usage.
    uint32_t tsdbFileSize = (uint32_t)f_size(&mTsdbFile);
    static const uint32_t MIN_PART = 32UL * 1024 * 1024;  // 32 MB minimum
    uint32_t tsdbPartSize = tsdbFileSize * 2;
    if (tsdbPartSize < tsdbFileSize + MIN_PART) tsdbPartSize = tsdbFileSize + MIN_PART;
    // Align up to sector boundary
    tsdbPartSize = ((tsdbPartSize + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;
    // Cap at 2GB
    if (tsdbPartSize > TSDB_MAX_SIZE) tsdbPartSize = TSDB_MAX_SIZE;
    printf("[SdFal] TSDB partition: fsize=%luKB, part=%luMB\n",
           tsdbFileSize / 1024, tsdbPartSize / (1024 * 1024));

    // Set up FAL flash device
    strncpy(sFalDev.name, "sd_fal", FAL_DEV_NAME_MAX);
    sFalDev.addr = 0;
    sFalDev.len = tsdbPartSize + KVDB_SIZE;
    sFalDev.blk_size = SECTOR_SIZE;

    // TSDB partition (dynamic size, file grows on demand)
    strncpy(sFalParts[PART_TSDB].name, "tsdb", FAL_DEV_NAME_MAX);
    strncpy(sFalParts[PART_TSDB].flash_name, "sd_fal", FAL_DEV_NAME_MAX);
    sFalParts[PART_TSDB].offset = 0;
    sFalParts[PART_TSDB].len = tsdbPartSize;

    // KVDB partition (offset follows TSDB)
    strncpy(sFalParts[PART_KVDB].name, "kvdb", FAL_DEV_NAME_MAX);
    strncpy(sFalParts[PART_KVDB].flash_name, "sd_fal", FAL_DEV_NAME_MAX);
    sFalParts[PART_KVDB].offset = tsdbPartSize;
    sFalParts[PART_KVDB].len = KVDB_SIZE;

    sFalInited = true;
    mInitOk = true;
    return true;
}

bool SdFalAdapter::openOrCreate(FIL* fp, const char* path, uint32_t size,
                                 uint8_t* bitmap, uint32_t bitmapBytes) {
    // Retry open in case of transient SDIO errors
    for (int attempt = 0; attempt < 3; attempt++) {
        FRESULT fr = f_open(fp, path, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
        if (fr == FR_OK) {
            uint32_t fsize = (uint32_t)f_size(fp);
            if (size == 0) {
                // Auto-grow mode: file-size tracking (no bitmap needed)
                // Pad to sector boundary so isMaterialized works for partial last sector
                // (power loss can leave file mid-sector)
                uint32_t aligned = ((fsize + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;
                if (aligned > fsize && fsize > 0) {
                    // Fill gap with 0xFF (erased flash state)
                    static uint8_t pad[256] __attribute__((aligned(4)));
                    memset(pad, 0xFF, sizeof(pad));
                    fp->err = 0;
                    f_lseek(fp, fsize);
                    uint32_t gap = aligned - fsize;
                    while (gap > 0) {
                        UINT toWrite = (gap > sizeof(pad)) ? sizeof(pad) : (UINT)gap;
                        UINT written;
                        fp->err = 0;
                        f_write(fp, pad, toWrite, &written);
                        if (written == 0) break;
                        gap -= written;
                    }
                    f_sync(fp);
                    printf("[SdFal] Opened %s (auto-grow), fsize=%lu→%lu (padded)\n",
                           path, fsize, (uint32_t)f_size(fp));
                } else {
                    printf("[SdFal] Opened %s (auto-grow), fsize=%lu\n", path, fsize);
                }
                return true;
            }
            // Fixed size mode
            if (fsize == size) {
                if (bitmap) memset(bitmap, 0xFF, bitmapBytes);
                return true;
            }
            f_close(fp);
            f_unlink(path);
            break;  // File exists but wrong size — delete and recreate
        }
        if (fr == FR_NO_FILE) break;  // Doesn't exist — create below
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    uint32_t tickStart = xTaskGetTickCount();
    FRESULT fr = f_open(fp, path, FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
    if (fr != FR_OK) {
        printf("[SdFal] f_open failed: %d\n", fr);
        return false;
    }

    if (size > 0) {
        // Fixed size: pre-allocate with f_expand (for KVDB)
        printf("[SdFal] Creating %s, size=%lu bytes...\n", path, size);
        fr = f_expand(fp, size, 1);
        uint32_t elapsed = xTaskGetTickCount() - tickStart;
        if (fr != FR_OK) {
            printf("[SdFal] f_expand failed: %d after %lu ms\n", fr, elapsed);
            f_close(fp);
            f_unlink(path);
            return false;
        }
        printf("[SdFal] Created %s OK (fixed), took %lu ms\n", path, elapsed);
    } else {
        // Auto-grow: empty file, no pre-allocation
        uint32_t elapsed = xTaskGetTickCount() - tickStart;
        printf("[SdFal] Created %s OK (auto-grow), took %lu ms\n", path, elapsed);
    }
    // Bitmap stays all-zero: all sectors are virtual
    return true;
}

int SdFalAdapter::read(int partId, uint32_t offset, uint8_t* buf, size_t size) {
    if (!mInitOk) return -1;

    // Auto-grow TSDB partition before FlashDB checks boundary
    if (partId == PART_TSDB) {
        growTsdbIfNeeded(offset + (uint32_t)size);

        // Fast path: fake FULL header for known-full sectors (skip SD card I/O).
        // ONLY during fdb_tsdb_init scan — check_sec_hdr_cb only needs status+magic.
        // Post-init queries (fdb_tsl_iter_by_time) need full header with end_idx/end_time,
        // so the fast path must be disabled after init completes.
        if (mInitScanActive && (offset % SECTOR_SIZE) == 0 && size <= 64) {
            uint32_t fsize = (uint32_t)f_size(&mTsdbFile);
            if (fsize >= SECTOR_SIZE * 2 && offset + SECTOR_SIZE * 2 <= fsize) {
                static uint32_t sFastCnt = 0;
                sFastCnt++;
                if (sFastCnt <= 3) {
                    printf("[SdFal] Fast FULL: sec=%lu size=%u fsize=%lu\n",
                           offset / SECTOR_SIZE, (unsigned)size, fsize);
                }
                memset(buf, 0xFF, size);
                buf[0] = 0x1F;  // FDB_SECTOR_STORE_FULL
                if (size >= 8) {
                    // Magic at offset 4 (ARM struct padding: uint8_t[1] + 3 pad + uint32_t)
                    buf[4] = 0x54; buf[5] = 0x53; buf[6] = 0x4C; buf[7] = 0x30; // TSL0
                }
                return 0;
            }
        }
    }

    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(500)) != pdTRUE) return -1;

    FIL* fp = (partId == PART_TSDB) ? &mTsdbFile : &mKvdbFile;
    size_t remaining = size;
    uint32_t pos = offset;
    uint8_t* dst = buf;

    while (remaining > 0) {
        uint32_t sectorIdx = pos / SECTOR_SIZE;
        uint32_t inSector  = pos % SECTOR_SIZE;
        uint32_t chunk     = SECTOR_SIZE - inSector;
        if (chunk > remaining) chunk = (uint32_t)remaining;

        if (!isMaterialized(partId, sectorIdx)) {
            // Virtual sector — return erased flash with fake sector header
            memset(dst, 0xFF, chunk);
            if (inSector < FAKE_HDR_SIZE) {
                const uint8_t* hdr = (partId == PART_TSDB) ? sTsdbFakeHdr : sKvdbFakeHdr;
                uint32_t copyLen = FAKE_HDR_SIZE - inSector;
                if (copyLen > chunk) copyLen = chunk;
                memcpy(dst, hdr + inSector, copyLen);
            }
        } else {
            // Materialized — read from file
            if (partId == PART_TSDB && inSector == 0) {
                static uint32_t sDiskHdrCnt = 0;
                sDiskHdrCnt++;
                if (sDiskHdrCnt <= 5) {
                    printf("[SdFal] Disk sec=%lu chunk=%lu\n", sectorIdx, chunk);
                }
            }
            bool ok = false;
            for (int attempt = 0; attempt < 3; attempt++) {
                fp->err = 0;
                FRESULT fr = f_lseek(fp, pos);
                if (fr != FR_OK) { vTaskDelay(1); continue; }
                fp->err = 0;
                UINT bytesRead;
                fr = f_read(fp, dst, chunk, &bytesRead);
                if (fr == FR_OK && bytesRead == chunk) { ok = true; break; }
                vTaskDelay(1);
            }
            if (!ok) { xSemaphoreGive(mMutex); return -1; }
        }

        dst       += chunk;
        pos       += chunk;
        remaining -= chunk;
    }

    xSemaphoreGive(mMutex);
    return 0;
}

int SdFalAdapter::write(int partId, uint32_t offset, const uint8_t* buf, size_t size) {
    if (!mInitOk) { printf("[FAL] write: not init\n"); return -1; }
    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(500)) != pdTRUE) { printf("[FAL] write: mutex timeout\n"); return -1; }

    FIL* fp = (partId == PART_TSDB) ? &mTsdbFile : &mKvdbFile;

    // Debug: Check file state
    if (fp->obj.fs == NULL) {
        printf("[FAL] write: ERROR file not opened!\n");
        xSemaphoreGive(mMutex);
        return -1;
    }

    // Auto-grow TSDB partition when approaching limit
    if (partId == PART_TSDB) {
        growTsdbIfNeeded(offset + (uint32_t)size);
    }

    // Materialize any virtual sectors touched by this write
    uint32_t startSec = offset / SECTOR_SIZE;
    uint32_t endSec   = (offset + size - 1) / SECTOR_SIZE;
    static uint32_t sMatFailCnt = 0;
    for (uint32_t s = startSec; s <= endSec; s++) {
        if (!isMaterialized(partId, s)) {
            if (!materializeSector(fp, s)) {
                if (++sMatFailCnt <= 5) printf("[FAL] write: materialize sec %lu FAILED (total failures: %lu)\n", s, sMatFailCnt);
                xSemaphoreGive(mMutex);
                return -1;
            }
        }
    }

    // Write actual data
    for (int attempt = 0; attempt < 3; attempt++) {
        fp->err = 0;
        FRESULT fr = f_lseek(fp, offset);
        if (fr != FR_OK) { printf("[FAL] write: seek err %d\n", fr); vTaskDelay(1); continue; }

        fp->err = 0;
        UINT bytesWritten = 0;
        fr = f_write(fp, buf, size, &bytesWritten);
        if (fr == FR_OK && bytesWritten == size) {
            xSemaphoreGive(mMutex);
            return 0;
        }
        printf("[FAL] write: f_write err %d, wrote %u/%u\n", fr, bytesWritten, (unsigned)size);
        vTaskDelay(1);
    }

    printf("[FAL] write: FAILED after 3 attempts at off=%lu size=%u\n", offset, (unsigned)size);
    xSemaphoreGive(mMutex);
    return -1;
}

int SdFalAdapter::erase(int partId, uint32_t offset, size_t size) {
    if (!mInitOk) return -1;
    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return -1;

    FIL* fp = (partId == PART_TSDB) ? &mTsdbFile : &mKvdbFile;
    uint32_t startSec = offset / SECTOR_SIZE;
    uint32_t endSec   = (offset + size - 1) / SECTOR_SIZE;

    // DMA requires 4-byte aligned buffer
    static uint8_t fill[256] __attribute__((aligned(4)));
    memset(fill, 0xFF, sizeof(fill));

    for (uint32_t s = startSec; s <= endSec; s++) {
        if (isMaterialized(partId, s)) {
            // Sector has data on disk — must write 0xFF so FlashDB sees
            // it as erased after power cycle (bitmap is RAM-only).
            uint32_t sectorOff = s * SECTOR_SIZE;
            fp->err = 0;
            FRESULT fr = f_lseek(fp, sectorOff);
            if (fr != FR_OK) { xSemaphoreGive(mMutex); return -1; }

            uint32_t rem = SECTOR_SIZE;
            while (rem > 0) {
                UINT toWrite = (rem > sizeof(fill)) ? sizeof(fill) : (UINT)rem;
                UINT written;
                fp->err = 0;
                fr = f_write(fp, fill, toWrite, &written);
                if (fr != FR_OK || written != toWrite) {
                    xSemaphoreGive(mMutex);
                    return -1;
                }
                rem -= written;
            }
            // Keep materialized — disk now has valid 0xFF content.
            // No materializeSector() needed on next write.
        }
        // Virtual sectors already read as 0xFF — nothing to do.
    }

    // No f_sync here — periodic sync handles flushing to SD card.
    xSemaphoreGive(mMutex);
    return 0;
}

void SdFalAdapter::sync() {
    if (!mInitOk) return;
    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    mTsdbFile.err = 0;
    f_sync(&mTsdbFile);
    mKvdbFile.err = 0;
    f_sync(&mKvdbFile);
    xSemaphoreGive(mMutex);
}

// ---- Lazy Virtual FAL bitmap helpers ----

bool SdFalAdapter::isMaterialized(int partId, uint32_t sectorIdx) {
    if (partId == PART_TSDB) {
        // TSDB: file-size tracking — sector is materialized if file covers it
        uint32_t sectorEnd = (sectorIdx + 1) * SECTOR_SIZE;
        return f_size(&mTsdbFile) >= sectorEnd;
    }
    // KVDB: bitmap tracking
    return (mKvdbBitmap[sectorIdx / 8] >> (sectorIdx % 8)) & 1;
}

void SdFalAdapter::setMaterialized(int partId, uint32_t sectorIdx, bool value) {
    if (partId == PART_TSDB) return;  // TSDB uses file-size tracking, no-op
    // KVDB: bitmap tracking
    if (value)
        mKvdbBitmap[sectorIdx / 8] |=  (1u << (sectorIdx % 8));
    else
        mKvdbBitmap[sectorIdx / 8] &= ~(1u << (sectorIdx % 8));
}

bool SdFalAdapter::materializeSector(FIL* fp, uint32_t sectorIdx) {
    uint32_t sectorOff = sectorIdx * SECTOR_SIZE;
    uint32_t sectorEnd = sectorOff + SECTOR_SIZE;

    // Auto-extend file if sector is beyond current file size
    if (sectorEnd > f_size(fp)) {
        fp->err = 0;
        FRESULT fr = f_lseek(fp, sectorEnd - 1);
        if (fr != FR_OK) {
            static uint8_t sCnt = 0;
            if (++sCnt <= 3) printf("[FAL] materialize: extend seek sec %lu FAILED fr=%d fsize=%lu\n",
                sectorIdx, fr, (uint32_t)f_size(fp));
            return false;
        }
        uint8_t zero = 0;
        UINT bw;
        fp->err = 0;
        fr = f_write(fp, &zero, 1, &bw);
        if (fr != FR_OK || bw != 1) {
            static uint8_t sCnt2 = 0;
            if (++sCnt2 <= 3) printf("[FAL] materialize: extend write FAILED fr=%d\n", fr);
            return false;
        }
    }

    // Write 0xFF to entire sector (simulates erased NOR flash)
    fp->err = 0;
    FRESULT fr = f_lseek(fp, sectorOff);
    if (fr != FR_OK) {
        static uint8_t sCnt3 = 0;
        if (++sCnt3 <= 3) printf("[FAL] materialize: seek sec %lu off=%lu FAILED fr=%d\n",
            sectorIdx, sectorOff, fr);
        return false;
    }

    // DMA requires 4-byte aligned buffer
    static uint8_t fill[256] __attribute__((aligned(4)));
    memset(fill, 0xFF, sizeof(fill));
    uint32_t rem = SECTOR_SIZE;
    while (rem > 0) {
        UINT toWrite = (rem > sizeof(fill)) ? sizeof(fill) : (UINT)rem;
        UINT written;
        fp->err = 0;
        fr = f_write(fp, fill, toWrite, &written);
        if (fr != FR_OK || written != toWrite) {
            static uint8_t sCnt4 = 0;
            if (++sCnt4 <= 3) printf("[FAL] materialize: write sec %lu FAILED fr=%d wrote=%u/%u\n",
                sectorIdx, fr, written, toWrite);
            return false;
        }
        rem -= written;
    }

    // Overlay fake sector header at sector start (so FlashDB reads
    // consistent header after materialization)
    fp->err = 0;
    f_lseek(fp, sectorOff);
    int partId = (fp == &mTsdbFile) ? PART_TSDB : PART_KVDB;
    const uint8_t* hdr = (partId == PART_TSDB) ? sTsdbFakeHdr : sKvdbFakeHdr;
    UINT hdrWritten;
    fp->err = 0;
    f_write(fp, hdr, FAKE_HDR_SIZE, &hdrWritten);

    setMaterialized(partId, sectorIdx, true);
    return true;
}

void SdFalAdapter::growTsdbIfNeeded(uint32_t writeEnd) {
    uint32_t currentLen = sFalParts[PART_TSDB].len;
    // Grow when file reaches within 25% of partition end.
    // FlashDB checks sector availability internally BEFORE calling FAL read/write,
    // so we must grow well ahead of the boundary to prevent FDB_SAVED_FULL.
    uint32_t threshold = currentLen / 4;  // 25% headroom
    if (threshold < 1024 * 1024) threshold = 1024 * 1024;  // min 1MB
    if (writeEnd + threshold < currentLen) return;
    if (currentLen >= TSDB_MAX_SIZE) return;

    // Double the partition (exponential growth) for fewer grow events
    uint32_t newLen = currentLen * 2;
    if (newLen < currentLen + 8UL * 1024 * 1024) newLen = currentLen + 8UL * 1024 * 1024;
    if (newLen > TSDB_MAX_SIZE) newLen = TSDB_MAX_SIZE;

    sFalParts[PART_TSDB].len = newLen;
    sFalParts[PART_KVDB].offset = newLen;
    sFalDev.len = newLen + KVDB_SIZE;

    printf("[SdFal] TSDB grow: %luMB → %luMB\n",
           currentLen / (1024 * 1024), newLen / (1024 * 1024));
}

bool SdFalAdapter::forceGrowTsdb() {
    uint32_t currentLen = sFalParts[PART_TSDB].len;
    if (currentLen >= TSDB_MAX_SIZE) return false;

    static const uint32_t GROW_SIZE = 8UL * 1024 * 1024;
    uint32_t newLen = currentLen + GROW_SIZE;
    if (newLen > TSDB_MAX_SIZE) newLen = TSDB_MAX_SIZE;

    sFalParts[PART_TSDB].len = newLen;
    sFalParts[PART_KVDB].offset = newLen;
    sFalDev.len = newLen + KVDB_SIZE;

    printf("[SdFal] Force grow TSDB: %luMB → %luMB\n",
           currentLen / (1024 * 1024), newLen / (1024 * 1024));
    return true;
}

bool SdFalAdapter::reopenTsdb(const char* renameTo) {
    if (xSemaphoreTake(mMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return false;

    // Sync and close current TSDB file
    mTsdbFile.err = 0;
    f_sync(&mTsdbFile);
    f_close(&mTsdbFile);

    // Rename old file to dated name (e.g. "tsdb_20260312.db")
    if (renameTo) {
        FRESULT fr = f_rename(TSDB_PATH, renameTo);
        if (fr != FR_OK && fr != FR_NO_FILE) {
            printf("[SdFal] rename %s → %s failed: %d\n", TSDB_PATH, renameTo, fr);
        } else {
            printf("[SdFal] Renamed %s → %s\n", TSDB_PATH, renameTo);
        }
    }

    // Reset TSDB partition to initial 8MB (fresh file, fast init)
    static const uint32_t INITIAL_PART = 8UL * 1024 * 1024;
    sFalParts[PART_TSDB].len = INITIAL_PART;
    sFalParts[PART_KVDB].offset = INITIAL_PART;
    sFalDev.len = INITIAL_PART + KVDB_SIZE;

    // Open fresh TSDB file (auto-grow, no pre-alloc, file-size tracking)
    bool ok = openOrCreate(&mTsdbFile, TSDB_PATH, 0, NULL, 0);

    xSemaphoreGive(mMutex);
    return ok;
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
