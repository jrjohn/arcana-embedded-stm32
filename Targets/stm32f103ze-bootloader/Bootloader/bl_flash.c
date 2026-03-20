/**
 * @file bl_flash.c
 * @brief Bootloader flash operations — erase App region + program + verify
 */

#include "bl_flash.h"
#include "stm32f1xx_hal.h"
#include "ota_header.h"
#include <string.h>

int bl_flash_erase_app(uint32_t num_pages)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0;

    /* Defense-in-depth: never erase key store pages */
    uint32_t max_pages = (KEY_STORE_BASE - APP_FLASH_BASE) / FLASH_PAGE_SIZE;
    if (num_pages > max_pages) num_pages = max_pages;

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) return -1;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = APP_FLASH_BASE;
    erase.NbPages = num_pages;

    status = HAL_FLASHEx_Erase(&erase, &page_error);

    HAL_FLASH_Lock();

    if (status != HAL_OK || page_error != 0xFFFFFFFF) {
        return -1;
    }
    return 0;
}

int bl_flash_program(uint32_t dest_addr, const uint8_t* data, uint32_t len)
{
    HAL_StatusTypeDef status;

    if (dest_addr < APP_FLASH_BASE || dest_addr >= APP_FLASH_END) return -1;

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) return -1;

    /* Program word-by-word (STM32F1 requires 16-bit halfword programming) */
    uint32_t i;
    for (i = 0; i < len; i += 2) {
        uint16_t halfword;
        if (i + 1 < len) {
            halfword = (uint16_t)data[i] | ((uint16_t)data[i + 1] << 8);
        } else {
            /* Pad last byte with 0xFF */
            halfword = (uint16_t)data[i] | 0xFF00;
        }

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                   dest_addr + i, halfword);
        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}

int bl_flash_verify(uint32_t addr, const uint8_t* data, uint32_t len)
{
    const uint8_t* flash = (const uint8_t*)addr;
    for (uint32_t i = 0; i < len; i++) {
        if (flash[i] != data[i]) {
            return -1;
        }
    }
    return 0;
}
