/**
 * @file sd_diskio.c
 * @brief FatFS disk I/O layer for SDIO SD card on STM32F103
 *
 * Implements the diskio.h interface required by FatFS.
 * Directly accesses SDIO via HAL_SD functions.
 */

#include "diskio.h"
#include "ff.h"
#include "stm32f1xx_hal.h"

/* External SD handle — initialized by SdCard driver */
extern SD_HandleTypeDef g_hsd;
extern volatile uint8_t g_sd_ready;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (!g_sd_ready) return STA_NOINIT;
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    /* SD card is initialized by SdCard::initHAL() before FatFS mount */
    if (!g_sd_ready) return STA_NOINIT;
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || !g_sd_ready) return RES_NOTRDY;

    if (HAL_SD_ReadBlocks(&g_hsd, buff, sector, count, 5000) != HAL_OK) {
        return RES_ERROR;
    }

    /* Wait for card to return to transfer state */
    uint32_t timeout = HAL_GetTick() + 5000;
    while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() > timeout) return RES_ERROR;
    }

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0 || !g_sd_ready) return RES_NOTRDY;

    if (HAL_SD_WriteBlocks(&g_hsd, (uint8_t *)buff, sector, count, 5000) != HAL_OK) {
        return RES_ERROR;
    }

    uint32_t timeout = HAL_GetTick() + 5000;
    while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() > timeout) return RES_ERROR;
    }

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0 || !g_sd_ready) return RES_NOTRDY;

    HAL_SD_CardInfoTypeDef info;
    DRESULT res = RES_ERROR;

    switch (cmd) {
    case CTRL_SYNC:
        res = RES_OK;
        break;

    case GET_SECTOR_COUNT:
        HAL_SD_GetCardInfo(&g_hsd, &info);
        *(DWORD *)buff = info.LogBlockNbr;
        res = RES_OK;
        break;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        res = RES_OK;
        break;

    case GET_BLOCK_SIZE:
        HAL_SD_GetCardInfo(&g_hsd, &info);
        *(DWORD *)buff = info.LogBlockSize / 512;
        res = RES_OK;
        break;

    default:
        res = RES_PARERR;
        break;
    }

    return res;
}
