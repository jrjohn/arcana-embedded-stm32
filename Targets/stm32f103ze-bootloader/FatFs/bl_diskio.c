/**
 * @file bl_diskio.c
 * @brief Bootloader FatFS disk I/O — polling-only SDIO (no DMA, no RTOS)
 *
 * Simple and reliable: all reads and writes use HAL polling mode.
 * Slower than DMA but safe for bootloader (no interrupts needed).
 */

#include "ff.h"
#include "diskio.h"
#include "stm32f1xx_hal.h"

/* SD handle — initialized in main */
SD_HandleTypeDef g_bl_hsd;

/* Slow SDIO clock for reliable polling: 72MHz / (17+2) ≈ 3.8MHz */
#define SDIO_CLKDIV_POLL  17U

static volatile DSTATUS sd_stat = STA_NOINIT;

/*-----------------------------------------------------------------------*/
/* Initialize SD card                                                     */
/*-----------------------------------------------------------------------*/
static int bl_sd_hw_init(void)
{
    g_bl_hsd.Instance = SDIO;
    g_bl_hsd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    g_bl_hsd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    g_bl_hsd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    g_bl_hsd.Init.BusWide = SDIO_BUS_WIDE_1B;
    g_bl_hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    g_bl_hsd.Init.ClockDiv = SDIO_CLKDIV_POLL;

    /* Clear any stale state from app or previous boot */
    SDIO->POWER = 0;
    SDIO->CLKCR = 0;
    SDIO->DCTRL = 0;
    SDIO->ICR = 0x7FF;
    HAL_Delay(50);

    if (HAL_SD_Init(&g_bl_hsd) != HAL_OK) {
        return -1;
    }

    /* Switch to 4-bit bus for better throughput */
    if (HAL_SD_ConfigWideBusOperation(&g_bl_hsd, SDIO_BUS_WIDE_4B) != HAL_OK) {
        /* 1-bit mode still works, continue */
    }

    return 0;
}

/*-----------------------------------------------------------------------*/
/* FatFS disk_status                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return sd_stat;
}

/*-----------------------------------------------------------------------*/
/* FatFS disk_initialize                                                  */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    if (bl_sd_hw_init() == 0) {
        sd_stat = 0;
    } else {
        sd_stat = STA_NOINIT;
    }
    return sd_stat;
}

/*-----------------------------------------------------------------------*/
/* FatFS disk_read — polling                                              */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    (void)pdrv;

    /* Ensure clean state */
    g_bl_hsd.Instance->DCTRL = 0;
    __HAL_SD_CLEAR_FLAG(&g_bl_hsd, SDIO_STATIC_FLAGS);

    HAL_StatusTypeDef st = HAL_SD_ReadBlocks(&g_bl_hsd, buff, sector, count, 5000);
    if (st != HAL_OK) return RES_ERROR;

    /* Wait for card to return to transfer state */
    uint32_t timeout = HAL_GetTick() + 2000;
    while (HAL_SD_GetCardState(&g_bl_hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() > timeout) return RES_ERROR;
    }

    /* CRITICAL: clear DCTRL after polling read — HAL leaves DTEN set */
    g_bl_hsd.Instance->DCTRL = 0;

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* FatFS disk_write — polling                                             */
/*-----------------------------------------------------------------------*/
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    (void)pdrv;

    g_bl_hsd.Instance->DCTRL = 0;
    __HAL_SD_CLEAR_FLAG(&g_bl_hsd, SDIO_STATIC_FLAGS);

    HAL_StatusTypeDef st = HAL_SD_WriteBlocks(&g_bl_hsd, (uint8_t*)buff, sector, count, 5000);
    if (st != HAL_OK) return RES_ERROR;

    uint32_t timeout = HAL_GetTick() + 2000;
    while (HAL_SD_GetCardState(&g_bl_hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() > timeout) return RES_ERROR;
    }

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* FatFS disk_ioctl                                                       */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    (void)pdrv;
    HAL_SD_CardInfoTypeDef info;

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            HAL_SD_GetCardInfo(&g_bl_hsd, &info);
            *(DWORD*)buff = info.LogBlockNbr;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            HAL_SD_GetCardInfo(&g_bl_hsd, &info);
            *(DWORD*)buff = info.LogBlockSize / 512;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    /* Fixed timestamp: 2026-03-17 00:00:00 */
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)3 << 21) | ((DWORD)17 << 16);
}
