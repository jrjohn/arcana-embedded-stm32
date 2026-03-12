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
 *   - "tsdb": time-series sensor data
 *   - "kvdb": upload tracking key-value pairs
 *
 * Lazy Virtual FAL: files are allocated with f_expand() (zero DMA at startup).
 * A RAM bitmap tracks materialized sectors. Virtual sectors return 0xFF on read;
 * sectors are materialized (written to disk) on first write.
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

    // Partition IDs
    static const int PART_TSDB = 0;
    static const int PART_KVDB = 1;

    // Partition sizes
    static const uint32_t TSDB_SIZE   = 8 * 1024 * 1024;     // 8 MB (2048 sectors)
    static const uint32_t KVDB_SIZE   = 64 * 1024;           // 64 KB
    static const uint32_t SECTOR_SIZE = 4096;                // 4 KB

    // Lazy Virtual FAL bitmap dimensions
    static const uint32_t TSDB_SECTORS = TSDB_SIZE / SECTOR_SIZE;
    static const uint32_t KVDB_SECTORS = KVDB_SIZE / SECTOR_SIZE;
    static const uint32_t TSDB_BITMAP_BYTES = (TSDB_SECTORS + 7) / 8;
    static const uint32_t KVDB_BITMAP_BYTES = (KVDB_SECTORS + 7) / 8;

private:
    SdFalAdapter();
    ~SdFalAdapter();
    SdFalAdapter(const SdFalAdapter&);
    SdFalAdapter& operator=(const SdFalAdapter&);

    bool openOrCreate(FIL* fp, const char* path, uint32_t size,
                      uint8_t* bitmap, uint32_t bitmapBytes);

    // Lazy Virtual FAL: bitmap tracks materialized sectors
    // 0 = virtual (read returns 0xFF), 1 = materialized (read from disk)
    bool isMaterialized(int partId, uint32_t sectorIdx) const;
    void setMaterialized(int partId, uint32_t sectorIdx, bool value);
    bool materializeSector(FIL* fp, uint32_t sectorIdx);

    FIL mTsdbFile;
    FIL mKvdbFile;
    bool mInitOk;

    uint8_t mTsdbBitmap[TSDB_BITMAP_BYTES];
    uint8_t mKvdbBitmap[KVDB_BITMAP_BYTES];

    StaticSemaphore_t mMutexBuffer;
    SemaphoreHandle_t mMutex;

    static const char* TSDB_PATH;
    static const char* KVDB_PATH;
};

} // namespace storage
} // namespace arcana
