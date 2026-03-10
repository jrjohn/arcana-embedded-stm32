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
 *   - "tsdb": 4MB file for time-series sensor data
 *   - "kvdb": 64KB file for upload tracking key-value pairs
 *
 * Files are pre-allocated with 0xFF on first use (simulating erased flash).
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

    // Partition IDs
    static const int PART_TSDB = 0;
    static const int PART_KVDB = 1;

    // Partition sizes
    static const uint32_t TSDB_SIZE   = 256 * 1024;        // 256 KB (64 sectors)
    static const uint32_t KVDB_SIZE   = 64 * 1024;         // 64 KB
    static const uint32_t SECTOR_SIZE = 4096;              // 4 KB

private:
    SdFalAdapter();
    ~SdFalAdapter();
    SdFalAdapter(const SdFalAdapter&);
    SdFalAdapter& operator=(const SdFalAdapter&);

    bool openOrCreate(FIL* fp, const char* path, uint32_t size);

    FIL mTsdbFile;
    FIL mKvdbFile;
    bool mInitOk;

    StaticSemaphore_t mMutexBuffer;
    SemaphoreHandle_t mMutex;

    static const char* TSDB_PATH;
    static const char* KVDB_PATH;
};

} // namespace storage
} // namespace arcana
