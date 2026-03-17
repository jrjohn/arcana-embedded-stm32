/**
 * @file bl_flash.h
 * @brief Bootloader flash erase + program + verify
 */
#ifndef BL_FLASH_H
#define BL_FLASH_H

#include <stdint.h>

/**
 * @brief Erase App flash region (0x08008000 onwards)
 * @param num_pages Number of 2KB pages to erase
 * @return 0 on success, -1 on error
 */
int bl_flash_erase_app(uint32_t num_pages);

/**
 * @brief Program flash from buffer, word-by-word
 * @param dest_addr Flash destination (must be word-aligned, >= APP_FLASH_BASE)
 * @param data Source data
 * @param len  Number of bytes (will be rounded up to word boundary)
 * @return 0 on success, -1 on error
 */
int bl_flash_program(uint32_t dest_addr, const uint8_t* data, uint32_t len);

/**
 * @brief Verify flash contents match buffer
 * @param addr Flash address to verify
 * @param data Expected data
 * @param len  Number of bytes
 * @return 0 if match, -1 if mismatch
 */
int bl_flash_verify(uint32_t addr, const uint8_t* data, uint32_t len);

#endif /* BL_FLASH_H */
