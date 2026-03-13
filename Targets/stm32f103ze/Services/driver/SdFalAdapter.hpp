#pragma once

#include "ff.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdint.h>

namespace arcana {
namespace storage {

/**
 * FAL-to-FatFS adapter for FlashDB.
 *
 * Maps FlashDB's FAL (Flash Abstraction Layer) read/write/erase to
 * FatFS file operations on the SD card. Two partitions:
 *   - "tsdb": time-series sensor data (auto-growing file)
 *   - "kvdb": upload tracking key-value pairs (fixed size)
 *
 * Virtual Sector Headers: virtual sectors return fake "formatted empty"
 * headers so FlashDB skips format_all during init (~50ms vs 14s).
 * A RAM bitmap tracks materialized sectors. Sectors auto-extend the
 * file via f_lseek on first write. KVDB uses f_expand (fixed size).
 * A FreeRTOS mutex protects all FatFS operations.
 */
class SdFalAdapter {
public:
    static SdFalAdapter& getInstance();

    // Must be called after g_exfat_ready == 1
    bool init();

    // FAL operations (called from C-linkage fal_* functions)
    int read(int partId, uint32_t offset, uint8_t* buf, size_t size);
    int write(int partId, uint32_t offset, const uint8_t* buf, size_t size);
    int erase(int partId, uint32_t offset, size_t size);

    // Flush FatFS buffers to SD card. Call periodically (e.g. every 30-60s).
    void sync();

    // Close current TSDB file, rename to renameTo (if non-null), open fresh tsdb.fdb.
    // Resets TSDB bitmap (all virtual). Used for daily file rotation.
    bool reopenTsdb(const char* renameTo);

    // Enable/disable fast FULL sector header path (only safe during fdb_tsdb_init)
    void setInitScanActive(bool active) { mInitScanActive = active; }

    // Partition IDs
    static const int PART_TSDB = 0;
    static const int PART_KVDB = 1;

    // TSDB: auto-growing (4GB address space = FlashDB uint32_t max, file grows on demand)
    // KVDB: fixed 64KB
    static const uint32_t TSDB_MAX_SIZE = 0xFFFFFFFFUL;  // ~4 GB (uint32_t max)
    static const uint32_t KVDB_SIZE     = 64 * 1024;                   // 64 KB
    static const uint32_t SECTOR_SIZE   = 4096;                        // 4 KB

    // Fake sector header size (status + padding + magic)
    static const uint32_t FAKE_HDR_SIZE = 8;

    // KVDB bitmap (TSDB uses file-size tracking — no bitmap needed)
    static const uint32_t KVDB_SECTORS = KVDB_SIZE / SECTOR_SIZE;
    static const uint32_t KVDB_BITMAP_BYTES = (KVDB_SECTORS + 7) / 8;

private:
    SdFalAdapter();
    ~SdFalAdapter();
    SdFalAdapter(const SdFalAdapter&);
    SdFalAdapter& operator=(const SdFalAdapter&);

    // size==0: auto-grow (no f_expand), size>0: fixed (f_expand)
    bool openOrCreate(FIL* fp, const char* path, uint32_t size,
                      uint8_t* bitmap, uint32_t bitmapBytes);

    // Lazy Virtual FAL: TSDB uses file-size tracking, KVDB uses bitmap
    bool isMaterialized(int partId, uint32_t sectorIdx);
    void setMaterialized(int partId, uint32_t sectorIdx, bool value);
    bool materializeSector(FIL* fp, uint32_t sectorIdx);

    // Grow TSDB partition when approaching current limit
    void growTsdbIfNeeded(uint32_t writeEnd);

    FIL mTsdbFile;
    FIL mKvdbFile;
    bool mInitOk;
    bool mInitScanActive;  // Fast FULL headers only during fdb_tsdb_init scan

    uint8_t mKvdbBitmap[KVDB_BITMAP_BYTES];

    StaticSemaphore_t mMutexBuffer;
    SemaphoreHandle_t mMutex;

    static const char* TSDB_PATH;
    static const char* KVDB_PATH;
};

} // namespace storage
} // namespace arcana
