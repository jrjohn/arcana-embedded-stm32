/*
 * FlashDB configuration for STM32F103ZE SD card storage.
 * Uses FAL mode with a custom FatFS-backed FAL adapter.
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* using KVDB feature (upload tracking) */
#define FDB_USING_KVDB

/* using TSDB (Time series database) feature */
#define FDB_USING_TSDB

/* Use 64-bit timestamps (millisecond epoch) for 1KHz+ sample rates */
#define FDB_USING_TIMESTAMP_64BIT

/* Using FAL storage mode with custom SD card adapter */
#define FDB_USING_FAL_MODE

/* Byte-level write granularity (SD card supports arbitrary writes) */
#define FDB_WRITE_GRAN         1

/* MCU is Little Endian */
/* #define FDB_BIG_ENDIAN */

/* Disable debug prints to save flash */
/* #define FDB_DEBUG_ENABLE */

/* Redirect FDB_PRINT to nothing (no printf on bare-metal) */
#define FDB_PRINT(...)

/* Disable native assert (use FDB's own) */
/* #define FDB_USING_NATIVE_ASSERT */

/* Reduce KV name max to save RAM */
#define FDB_KV_NAME_MAX        16

/* Reduce cache sizes to save RAM */
#define FDB_KV_CACHE_TABLE_SIZE    0
#define FDB_SECTOR_CACHE_TABLE_SIZE 0

#endif /* _FDB_CFG_H_ */
