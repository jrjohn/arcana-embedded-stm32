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

/* Full SDIO + DMA + SD card re-initialization (software power cycle).
 * Called proactively every N writes AND reactively on write failure.
 * Equivalent to HAL_SD_DeInit + HAL_SD_Init + bus width config. */
#define SDIO_REINIT_INTERVAL 200
static volatile uint32_t g_sdio_write_count = 0;

static void sdio_reinit(void) {
    extern DMA_HandleTypeDef g_hdma_sdio;

    /* 1. Disable + reset DMA channel */
    __HAL_DMA_DISABLE(&g_hdma_sdio);
    while (g_hdma_sdio.Instance->CCR & DMA_CCR_EN) {}
    __HAL_DMA_CLEAR_FLAG(&g_hdma_sdio,
        DMA_FLAG_TC4 | DMA_FLAG_TE4 | DMA_FLAG_HT4 | DMA_FLAG_GL4);
    g_hdma_sdio.State = HAL_DMA_STATE_READY;
    g_hdma_sdio.Lock = HAL_UNLOCKED;

    /* 2. Clear SDIO data path + flags (keep card link alive for FatFS) */
    SDIO->DCTRL = 0;
    SDIO->MASK = 0;
    SDIO->ICR = 0xFFFFFFFF;
    SDMMC_CmdStopTransfer(g_hsd.Instance);
    SDIO->ICR = 0xFFFFFFFF;
    SDIO->DCTRL = 0;

    /* 3. Reset HAL state machine (but NOT the card) */
    g_hsd.State = HAL_SD_STATE_READY;
    g_hsd.Context = SD_CONTEXT_NONE;
    g_hsd.ErrorCode = HAL_SD_ERROR_NONE;

    /* 4. Wait for card to be ready */
    uint32_t t = HAL_GetTick() + 500;
    while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() > t) break;
    }
}

/* Expose reinit for external callers (AtsStorageService recovery) */
void sdio_force_reinit(void) {
    sdio_reinit();
    g_sdio_write_count = 0;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s) — DMA at full SDIO clock speed                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !g_sd_ready) return RES_NOTRDY;
    if (!g_sd_dma_sem) return RES_ERROR;

    /* Proactive SDIO reinit every N writes to prevent bus degradation */
    if (++g_sdio_write_count >= SDIO_REINIT_INTERVAL) {
        sdio_reinit();
        g_sdio_write_count = 0;
    }

    for (int retry = 0; retry < 3; retry++) {
        ensure_hal_ready();

        /* DMA write at full clock — CPU free during transfer.
         * Only TX direction used (reads stay polling), so no DMA
         * direction switching = no DATAEND hang. */
        MODIFY_REG(SDIO->CLKCR, 0xFFU, SDIO_CLKDIV_FAST);

        /* Clear stale semaphore */
        xSemaphoreTake(g_sd_dma_sem, 0);

        HAL_StatusTypeDef hal = HAL_SD_WriteBlocks_DMA(&g_hsd, (uint8_t *)buff,
                                                        sector, count);
        if (hal != HAL_OK) {
            sdio_reinit();
            g_sdio_write_count = 0;
            continue;
        }

        /* Wait for DMA + SDIO completion (semaphore from TxCpltCallback) */
        if (xSemaphoreTake(g_sd_dma_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
            sdio_reinit();
            g_sdio_write_count = 0;
            continue;
        }

        /* Check for DMA/SDIO errors */
        if (g_hsd.ErrorCode != HAL_SD_ERROR_NONE) {
            sdio_reinit();
            g_sdio_write_count = 0;
            continue;
        }

        SDIO->DCTRL = 0;
        __HAL_SD_CLEAR_FLAG(&g_hsd, SDIO_STATIC_FLAGS);

        /* Wait for card to finish internal programming */
        uint32_t timeout = HAL_GetTick() + 5000;
        while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
            if (HAL_GetTick() > timeout) break;
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
