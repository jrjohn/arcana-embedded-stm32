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
#include "task.h"
#include <stdio.h>

/* External SD handle — initialized by SdCard driver */
extern SD_HandleTypeDef g_hsd;
extern DMA_HandleTypeDef g_hdma_sdio;
extern volatile uint8_t g_sd_ready;
extern SemaphoreHandle_t g_sd_dma_sem;

/* Deep reinit is disabled during boot to avoid interfering with initial
 * FatFS mount + FlashDB init. Enabled by application after boot completes. */
volatile uint8_t g_sdio_reinit_enabled = 0;

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

/**
 * Deep SDIO re-initialization: full hardware reset + SD card re-init.
 * Called when normal retry logic (ensure_hal_ready + 3 retries) fails,
 * indicating SDIO peripheral degradation from prolonged DMA writes.
 *
 * Replicates the essential steps of SdCard::initHAL() without GPIO
 * init or semaphore creation (those are one-time).
 *
 * Returns 0 on success, -1 on failure or rate-limited.
 */
static int sdio_deep_reinit(void) {
    static uint32_t sReinitCnt = 0;
    static uint32_t sLastReinitTick = 0;

    /* Only attempt deep reinit after boot is complete */
    if (!g_sdio_reinit_enabled) return -1;

    /* Rate limit: at most once per 5 seconds to avoid hammering */
    uint32_t now = HAL_GetTick();
    if (sLastReinitTick != 0 && (now - sLastReinitTick) < 5000) {
        return -1;
    }
    sLastReinitTick = now;
    sReinitCnt++;
    printf("[SDIO] Deep reinit #%lu...\n", (unsigned long)sReinitCnt);

    /* 1. Disable SDIO/DMA interrupts during reset */
    HAL_NVIC_DisableIRQ(SDIO_IRQn);
    HAL_NVIC_DisableIRQ(DMA2_Channel4_5_IRQn);

    /* 2. Clear all SDIO registers (same as SdCard::initHAL) */
    SDIO->POWER  = 0;
    SDIO->CLKCR  = 0;
    SDIO->CMD    = 0;
    SDIO->DTIMER = 0;
    SDIO->DLEN   = 0;
    SDIO->DCTRL  = 0;
    SDIO->ICR    = 0x00C007FF;
    SDIO->MASK   = 0;

    /* 3. Clear DMA2 Channel 4 */
    DMA2_Channel4->CCR   = 0;
    DMA2_Channel4->CNDTR = 0;
    DMA2->IFCR = (0xFu << 12);

    /* 4. Wait for SD card to timeout any stuck transfer */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 5. Re-init HAL SD (400kHz, 1-bit for identification phase) */
    g_hsd.State = HAL_SD_STATE_RESET;
    g_hsd.ErrorCode = HAL_SD_ERROR_NONE;
    g_hsd.Init.ClockDiv = 178;
    g_hsd.Init.BusWide = SDIO_BUS_WIDE_1B;

    if (HAL_SD_Init(&g_hsd) != HAL_OK) {
        printf("[SDIO] Deep reinit FAILED: HAL_SD_Init err\n");
        HAL_NVIC_EnableIRQ(SDIO_IRQn);
        HAL_NVIC_EnableIRQ(DMA2_Channel4_5_IRQn);
        return -1;
    }

    /* 6. Re-configure DMA2 Channel 4 for SDIO */
    g_hdma_sdio.State = HAL_DMA_STATE_RESET;
    g_hdma_sdio.Lock = HAL_UNLOCKED;
    HAL_DMA_Init(&g_hdma_sdio);
    __HAL_LINKDMA(&g_hsd, hdmatx, g_hdma_sdio);
    __HAL_LINKDMA(&g_hsd, hdmarx, g_hdma_sdio);

    /* 7. Switch to 4-bit bus */
    if (HAL_SD_ConfigWideBusOperation(&g_hsd, SDIO_BUS_WIDE_4B) != HAL_OK) {
        printf("[SDIO] Deep reinit: 4-bit bus FAILED, continuing 1-bit\n");
    }

    /* 8. Set transfer clock (24MHz) */
    SDIO->CLKCR = (SDIO->CLKCR & ~0xFFU) | SDIO_CLKDIV_FAST;
    g_hsd.Init.ClockDiv = SDIO_CLKDIV_FAST;

    /* 9. Re-enable interrupts */
    HAL_NVIC_SetPriority(SDIO_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);
    HAL_NVIC_SetPriority(DMA2_Channel4_5_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA2_Channel4_5_IRQn);

    printf("[SDIO] Deep reinit #%lu OK\n", (unsigned long)sReinitCnt);
    return 0;
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
/* Single read attempt (shared by retry loop and post-reinit retry)      */
/*-----------------------------------------------------------------------*/
static DRESULT try_read(BYTE *buff, LBA_t sector, UINT count)
{
    ensure_hal_ready();

    MODIFY_REG(SDIO->CLKCR, 0xFFU, SDIO_CLKDIV_SLOW);
    HAL_StatusTypeDef hal = HAL_SD_ReadBlocks(&g_hsd, buff, sector, count, 5000);
    MODIFY_REG(SDIO->CLKCR, 0xFFU, SDIO_CLKDIV_FAST);

    /* Always clear DCTRL after polling read — HAL leaves DTEN set */
    SDIO->DCTRL = 0;
    __HAL_SD_CLEAR_FLAG(&g_hsd, SDIO_STATIC_FLAGS);

    if (hal != HAL_OK) return RES_ERROR;

    uint32_t timeout = HAL_GetTick() + 5000;
    while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() > timeout) break;
    }
    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s) — Polling at reduced SDIO clock                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !g_sd_ready) return RES_NOTRDY;

    for (int retry = 0; retry < 3; retry++) {
        if (try_read(buff, sector, count) == RES_OK) return RES_OK;
    }

    /* All 3 retries failed — deep SDIO reinit and one more attempt */
    if (sdio_deep_reinit() == 0) {
        if (try_read(buff, sector, count) == RES_OK) return RES_OK;
    }

    return RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Single write attempt (shared by retry loop and post-reinit retry)     */
/*-----------------------------------------------------------------------*/
static DRESULT try_write(const BYTE *buff, LBA_t sector, UINT count)
{
    static uint32_t sWriteFailCnt = 0;

    ensure_hal_ready();

    /* Clear any stale semaphore signal */
    xSemaphoreTake(g_sd_dma_sem, 0);

    HAL_StatusTypeDef hal_status = HAL_SD_WriteBlocks_DMA(&g_hsd, (uint8_t *)buff, sector, count);
    if (hal_status != HAL_OK) {
        if (++sWriteFailCnt <= 5) printf("[SDIO] WriteBlocks_DMA failed: %d\n", hal_status);
        return RES_ERROR;
    }

    /* Wait for DMA + SDIO completion (HAL_SD_TxCpltCallback gives semaphore) */
    if (xSemaphoreTake(g_sd_dma_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        if (++sWriteFailCnt <= 5) printf("[SDIO] DMA timeout\n");
        return RES_ERROR;
    }

    /* Check for transfer errors */
    if (g_hsd.ErrorCode != HAL_SD_ERROR_NONE) {
        if (++sWriteFailCnt <= 5) printf("[SDIO] SD error: 0x%08lX\n", g_hsd.ErrorCode);
        return RES_ERROR;
    }

    /* Wait for card to finish internal programming */
    uint32_t timeout = HAL_GetTick() + 5000;
    while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() > timeout) {
            if (++sWriteFailCnt <= 5) printf("[SDIO] Card state timeout\n");
            return RES_ERROR;
        }
    }

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s) — DMA at full SDIO clock speed                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !g_sd_ready) return RES_NOTRDY;
    if (!g_sd_dma_sem) return RES_ERROR;

    for (int retry = 0; retry < 3; retry++) {
        if (try_write(buff, sector, count) == RES_OK) return RES_OK;
    }

    /* All 3 retries failed — deep SDIO reinit and one more attempt */
    if (sdio_deep_reinit() == 0) {
        if (try_write(buff, sector, count) == RES_OK) return RES_OK;
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
