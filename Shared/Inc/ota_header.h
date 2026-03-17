/**
 * @file ota_header.h
 * @brief OTA metadata structure shared between App and Bootloader
 *
 * Pure C header - no C++ dependencies.
 * Both App (writes) and Bootloader (reads) use this format.
 *
 * File on SD card: ota_meta.bin (44 bytes, little-endian)
 */

#ifndef ARCANA_OTA_HEADER_H
#define ARCANA_OTA_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Magic values */
#define OTA_META_MAGIC      0x41524F54u  /* "AROT" (Arcana OTA) */
#define OTA_META_VERSION    1u
#define OTA_FLAG_MAGIC      0x4F544100u  /* "OTA\0" — stored in BKP DR2/DR3 */
#define OTA_FLAG_DR2_VALUE  0x4F54u      /* "OT" — BKP->DR2 (16-bit) */
#define OTA_FLAG_DR3_VALUE  0x4100u      /* "A\0" — BKP->DR3 (16-bit) */

/* App flash layout */
#define APP_FLASH_BASE      0x08008000u  /* After 32KB bootloader */
#define APP_FLASH_SIZE      (480u * 1024u)
#define APP_FLASH_END       (APP_FLASH_BASE + APP_FLASH_SIZE)

/* STM32F103ZE flash page size (guard: HAL may define this too) */
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE     2048u
#endif

/* SD card file paths */
#define OTA_FW_FILENAME     "firmware.bin"
#define OTA_META_FILENAME   "ota_meta.bin"
#define OTA_PREV_FILENAME   "fw_prev.bin"
#define OTA_STATUS_FILENAME "ota_status.txt"

/**
 * @brief OTA metadata written to SD card alongside firmware.bin
 *
 * Layout: 44 bytes, packed, little-endian
 * meta_crc covers bytes [0..39] (everything except meta_crc itself)
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;          /**< Must be OTA_META_MAGIC (0x41524F54) */
    uint8_t  version;        /**< Metadata format version (currently 1) */
    uint8_t  reserved[3];    /**< Reserved, set to 0 */
    uint32_t fw_size;        /**< firmware.bin size in bytes */
    uint32_t crc32;          /**< CRC-32 IEEE of firmware.bin (~crc32(0xFFFFFFFF,...)) */
    uint32_t target_addr;    /**< Target flash address (APP_FLASH_BASE) */
    char     fw_version[16]; /**< Version string, null-terminated */
    uint32_t timestamp;      /**< Unix epoch seconds */
    uint32_t meta_crc;       /**< CRC-32 of this struct [0..39] */
} ota_meta_t;

#define OTA_META_CRC_OFFSET  40u  /* Offset of meta_crc within ota_meta_t */

/**
 * @brief Validate SP and Reset_Handler from vector table
 * @param addr Base address to check (e.g. APP_FLASH_BASE)
 * @return 1 if valid app image, 0 otherwise
 */
static inline int ota_validate_app_image(uint32_t addr) {
    uint32_t sp    = *(volatile uint32_t*)addr;
    uint32_t reset = *(volatile uint32_t*)(addr + 4u);

    /* SP must be in RAM range (0x20000000 - 0x20010000 for 64KB) */
    if (sp < 0x20000000u || sp > 0x20010000u) return 0;
    /* Reset_Handler must be in App flash range */
    if (reset < addr || reset > APP_FLASH_END) return 0;

    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* ARCANA_OTA_HEADER_H */
