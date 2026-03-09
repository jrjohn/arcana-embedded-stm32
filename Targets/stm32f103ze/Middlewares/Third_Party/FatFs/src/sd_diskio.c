/**
 * @file sd_diskio.c
 * @brief FatFS disk I/O layer for SDIO SD card on STM32F103
 *
 * Implements the diskio.h interface required by FatFS.
 * Uses DMA for writes (polling fails at 24MHz SDIO clock due to FIFO underrun).
 */

#include "ff.h"
#include "diskio.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* External SD handle — initialized by SdCard driver */
extern SD_HandleTypeDef g_hsd;
extern volatile uint8_t g_sd_ready;
extern SemaphoreHandle_t g_sd_dma_sem;

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
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
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
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !g_sd_ready) return RES_NOTRDY;
    if (!g_sd_dma_sem) return RES_ERROR;

    /* Clear any stale semaphore signal */
    xSemaphoreTake(g_sd_dma_sem, 0);

    /* DMA write (polling fails at 24MHz SDIO clock due to FIFO underrun) */
    if (HAL_SD_WriteBlocks_DMA(&g_hsd, (uint8_t *)buff, sector, count) != HAL_OK) {
        return RES_ERROR;
    }

    /* Wait for DMA + SDIO completion (HAL_SD_TxCpltCallback gives semaphore) */
    if (xSemaphoreTake(g_sd_dma_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return RES_ERROR;
    }

    /* Check for transfer errors */
    if (g_hsd.ErrorCode != HAL_SD_ERROR_NONE) {
        return RES_ERROR;
    }

    /* Wait for card to finish internal programming */
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
