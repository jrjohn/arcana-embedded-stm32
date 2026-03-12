/**
 * @file sd_diskio.c
 * @brief FatFS disk I/O layer for SDIO SD card on STM32F103
 *
 * Implements the diskio.h interface required by FatFS.
 * - Reads: Polling at reduced SDIO clock (~4MHz) to avoid FIFO overflow
 * - Writes: DMA at full speed (24MHz)
 *
 * STM32F103 shares DMA2 Channel 4 between SDIO TX and RX. Mixing DMA reads
 * and DMA writes causes SDIO DATAEND to not fire for writes (the DMA direction
 * switch leaves the SDIO data path in a broken state). Using polling for reads
 * avoids this hardware limitation entirely.
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

/* Reduced SDIO clock divisor for polling reads.
 * 72MHz / (17+2) ≈ 3.8MHz — slow enough that the CPU can drain the FIFO
 * via polling even under FreeRTOS interrupt load. */
#define SDIO_CLKDIV_SLOW 17U

/* Fast SDIO clock divisor for DMA writes.
 * 72MHz / (1+2) = 24MHz — maximum Default Speed. */
#define SDIO_CLKDIV_FAST 1U

/* Ensure SDIO hardware and HAL state machine are both ready.
 * HAL_SD_ReadBlocks (polling) leaves SDIO->DCTRL.DTEN set after every read.
 * This blocks subsequent writes. Unconditionally clear the data path. */
static void ensure_hal_ready(void) {
    /* ALWAYS clear SDIO data path before any new operation. */
    g_hsd.Instance->DCTRL = 0;
    __HAL_SD_CLEAR_FLAG(&g_hsd, SDIO_STATIC_FLAGS);
    g_hsd.ErrorCode = HAL_SD_ERROR_NONE;

    if (g_hsd.State == HAL_SD_STATE_READY) return;

    /* Wait up to 1 second for natural completion */
    uint32_t t = HAL_GetTick() + 1000;
    while (g_hsd.State != HAL_SD_STATE_READY) {
        if (HAL_GetTick() > t) break;
    }
    if (g_hsd.State == HAL_SD_STATE_READY) return;

    /* Force-reset: abort DMA, reset SDIO, recover card */
    extern DMA_HandleTypeDef g_hdma_sdio;
    __HAL_DMA_DISABLE(&g_hdma_sdio);
    g_hdma_sdio.State = HAL_DMA_STATE_READY;
    g_hdma_sdio.Lock = HAL_UNLOCKED;

    g_hsd.Instance->DCTRL = 0;
    g_hsd.Instance->MASK = 0;
    __HAL_SD_CLEAR_FLAG(&g_hsd, SDIO_STATIC_FLAGS);

    g_hsd.State = HAL_SD_STATE_READY;
    g_hsd.Context = SD_CONTEXT_NONE;
    g_hsd.ErrorCode = HAL_SD_ERROR_NONE;

    /* Send STOP command to recover card from stuck RECEIVE/DATA state */
    SDMMC_CmdStopTransfer(g_hsd.Instance);

    /* Wait for card to return to TRANSFER state */
    t = HAL_GetTick() + 1000;
    while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() > t) break;
    }
}

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
/* Read Sector(s) — Polling at reduced SDIO clock                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !g_sd_ready) return RES_NOTRDY;

    for (int retry = 0; retry < 3; retry++) {
        ensure_hal_ready();

        /* Slow down SDIO clock for polling (avoid FIFO overflow under IRQ load) */
        MODIFY_REG(SDIO->CLKCR, 0xFFU, SDIO_CLKDIV_SLOW);

        HAL_StatusTypeDef hal = HAL_SD_ReadBlocks(&g_hsd, buff, sector, count, 5000);

        /* Restore fast clock */
        MODIFY_REG(SDIO->CLKCR, 0xFFU, SDIO_CLKDIV_FAST);

        /* Always clear DCTRL after polling read — HAL_SD_ReadBlocks can leave
         * DTEN set (stuck RXACT) if the transfer partially completes or the
         * data CRC arrives after the HAL timeout. */
        SDIO->DCTRL = 0;
        __HAL_SD_CLEAR_FLAG(&g_hsd, SDIO_STATIC_FLAGS);

        if (hal != HAL_OK) {
            continue;
        }

        /* Wait for card to return to transfer state */
        uint32_t timeout = HAL_GetTick() + 5000;
        while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
            if (HAL_GetTick() > timeout) break;
        }

        return RES_OK;
    }

    return RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s) — DMA at full SDIO clock speed                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !g_sd_ready) return RES_NOTRDY;
    if (!g_sd_dma_sem) return RES_ERROR;

    static uint32_t sWriteFailCnt = 0;
    static uint32_t sLastErr = 0;

    for (int retry = 0; retry < 3; retry++) {
        ensure_hal_ready();

        /* Clear any stale semaphore signal */
        xSemaphoreTake(g_sd_dma_sem, 0);

        HAL_StatusTypeDef hal_status = HAL_SD_WriteBlocks_DMA(&g_hsd, (uint8_t *)buff, sector, count);
        if (hal_status != HAL_OK) {
            if (++sWriteFailCnt <= 3) printf("[SDIO] WriteBlocks_DMA failed: %d\n", hal_status);
            continue;
        }

        /* Wait for DMA + SDIO completion (HAL_SD_TxCpltCallback gives semaphore) */
        if (xSemaphoreTake(g_sd_dma_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
            if (++sWriteFailCnt <= 3) printf("[SDIO] DMA timeout\n");
            continue;
        }

        /* Check for transfer errors */
        if (g_hsd.ErrorCode != HAL_SD_ERROR_NONE) {
            if (++sWriteFailCnt <= 3) {
                sLastErr = g_hsd.ErrorCode;
                printf("[SDIO] SD error: 0x%08lX\n", sLastErr);
            }
            continue;
        }

        /* Wait for card to finish internal programming */
        uint32_t timeout = HAL_GetTick() + 5000;
        while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
            if (HAL_GetTick() > timeout) {
                if (++sWriteFailCnt <= 3) printf("[SDIO] Card state timeout\n");
                break;
            }
        }

        return RES_OK;
    }

    return RES_ERROR;
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
