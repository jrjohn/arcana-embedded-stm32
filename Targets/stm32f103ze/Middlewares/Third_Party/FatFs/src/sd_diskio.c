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

static void sdio_reinit(void);  /* forward declaration */
static volatile int g_sd_dma_read_mode = 0;

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

        HAL_StatusTypeDef hal;

        if (g_sd_dma_read_mode && g_sd_dma_sem) {
            /* DMA read at full clock — used during upload (no concurrent writes) */
            MODIFY_REG(SDIO->CLKCR, 0xFFU, SDIO_CLKDIV_FAST);
            xSemaphoreTake(g_sd_dma_sem, 0);  /* drain stale */

            hal = HAL_SD_ReadBlocks_DMA(&g_hsd, buff, sector, count);
            if (hal != HAL_OK) {
                sdio_reinit();
                continue;
            }

            if (xSemaphoreTake(g_sd_dma_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
                sdio_reinit();
                continue;
            }

            if (g_hsd.ErrorCode != HAL_SD_ERROR_NONE) {
                sdio_reinit();
                continue;
            }
        } else {
            /* Polling read at reduced clock (safe for mixed read/write) */
            MODIFY_REG(SDIO->CLKCR, 0xFFU, SDIO_CLKDIV_SLOW);
            hal = HAL_SD_ReadBlocks(&g_hsd, buff, sector, count, 5000);
            MODIFY_REG(SDIO->CLKCR, 0xFFU, SDIO_CLKDIV_FAST);

            if (hal != HAL_OK) {
                SDIO->DCTRL = 0;
                __HAL_SD_CLEAR_FLAG(&g_hsd, SDIO_STATIC_FLAGS);
                continue;
            }
        }

        SDIO->DCTRL = 0;
        __HAL_SD_CLEAR_FLAG(&g_hsd, SDIO_STATIC_FLAGS);

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

/* --- DMA Read Mode (for upload — no writes during this window) --- */

void sd_enable_dma_reads(void) {
    extern DMA_HandleTypeDef g_hdma_sdio;
    sdio_reinit();
    /* Reconfigure DMA for read direction (PERIPH → MEMORY) */
    __HAL_DMA_DISABLE(&g_hdma_sdio);
    g_hdma_sdio.Init.Direction = DMA_PERIPH_TO_MEMORY;
    HAL_DMA_Init(&g_hdma_sdio);
    g_sd_dma_read_mode = 1;
}

void sd_disable_dma_reads(void) {
    extern DMA_HandleTypeDef g_hdma_sdio;
    sdio_reinit();
    /* Restore DMA for write direction (MEMORY → PERIPH) */
    __HAL_DMA_DISABLE(&g_hdma_sdio);
    g_hdma_sdio.Init.Direction = DMA_MEMORY_TO_PERIPH;
    HAL_DMA_Init(&g_hdma_sdio);
    g_sd_dma_read_mode = 0;
}

/* Full card re-enumeration after physical card swap (CMD0/CMD8/ACMD41).
 * Call after removing + reinserting SD card. */
void sd_card_full_reinit(void) {
    extern DMA_HandleTypeDef g_hdma_sdio;

    HAL_SD_DeInit(&g_hsd);
    HAL_Delay(100);

    /* Re-init (card identification + transfer mode) */
    if (HAL_SD_Init(&g_hsd) == HAL_OK) {
        HAL_SD_ConfigWideBusOperation(&g_hsd, SDIO_BUS_WIDE_4B);
        /* Re-link DMA (DeInit might have cleared it) */
        __HAL_LINKDMA(&g_hsd, hdmatx, g_hdma_sdio);
        __HAL_LINKDMA(&g_hsd, hdmarx, g_hdma_sdio);
    }
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

/*-----------------------------------------------------------------------*/
/* Get current time for FatFS timestamps (from STM32F103 RTC counter)    */
/*-----------------------------------------------------------------------*/
DWORD get_fattime(void)
{
    /* Read RTC counter (epoch seconds) */
    uint16_t h1 = RTC->CNTH;
    uint16_t l  = RTC->CNTL;
    uint16_t h2 = RTC->CNTH;
    if (h1 != h2) { l = RTC->CNTL; h1 = h2; }
    uint32_t epoch = ((uint32_t)h1 << 16) | l;

    /* If RTC not set (< year 2020), return fixed date */
    if (epoch < 1577836800U) {
        return ((DWORD)(2026 - 1980) << 25) | ((DWORD)3 << 21) | ((DWORD)18 << 16);
    }

    /* Howard Hinnant civil_from_days algorithm */
    uint32_t z = epoch / 86400 + 719468;
    uint32_t era = z / 146097;
    uint32_t doe = z - era * 146097;
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint32_t y = yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t day = doy - (153 * mp + 2) / 5 + 1;
    uint32_t mon = mp < 10 ? mp + 3 : mp - 9;
    if (mon <= 2) y++;

    uint32_t daySec = epoch % 86400;
    uint32_t hour = daySec / 3600;
    uint32_t min  = (daySec % 3600) / 60;
    uint32_t sec  = daySec % 60;

    return ((DWORD)(y - 1980) << 25) |
           ((DWORD)mon << 21) |
           ((DWORD)day << 16) |
           ((DWORD)hour << 11) |
           ((DWORD)min << 5) |
           ((DWORD)(sec / 2));
}
