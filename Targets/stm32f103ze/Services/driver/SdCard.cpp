#include "SdCard.hpp"
#include "FreeRTOS.h"
#include "semphr.h"

/* Globals for HAL SD + DMA (C linkage) */
extern "C" {
    SD_HandleTypeDef g_hsd;
    DMA_HandleTypeDef g_hdma_sdio;
    volatile uint8_t g_sd_ready = 0;

    static StaticSemaphore_t g_sd_dma_sem_buf;
    static SemaphoreHandle_t g_sd_dma_sem = 0;

    void HAL_SD_TxCpltCallback(SD_HandleTypeDef* hsd) {
        (void)hsd;
        BaseType_t woken = pdFALSE;
        if (g_sd_dma_sem) {
            xSemaphoreGiveFromISR(g_sd_dma_sem, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }

    void HAL_SD_ErrorCallback(SD_HandleTypeDef* hsd) {
        (void)hsd;
        BaseType_t woken = pdFALSE;
        if (g_sd_dma_sem) {
            xSemaphoreGiveFromISR(g_sd_dma_sem, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }
}

namespace arcana {

SdCard::SdCard()
    : mReady(false)
{
}

SdCard::~SdCard() {}

SdCard& SdCard::getInstance() {
    static SdCard sInstance;
    return sInstance;
}

void SdCard::initGpio() {
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};

    // SDIO_CK (PC12)
    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);

    // SDIO_CMD (PD2)
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOD, &gpio);

    // SDIO_D0-D3 (PC8-PC11)
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);
}

bool SdCard::initHAL() {
    initGpio();

    // Create DMA completion semaphore (static allocation)
    g_sd_dma_sem = xSemaphoreCreateBinaryStatic(&g_sd_dma_sem_buf);
    if (!g_sd_dma_sem) return false;

    // Enable DMA2 clock
    __HAL_RCC_DMA2_CLK_ENABLE();

    // SDIOCLK = HCLK = 72MHz
    // Init clock: 72MHz / (178+2) = 400kHz (SD spec requires ≤400kHz for init)
    g_hsd.Instance = SDIO;
    g_hsd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    g_hsd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    g_hsd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    g_hsd.Init.BusWide = SDIO_BUS_WIDE_1B;  // Must start 1-bit
    g_hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    g_hsd.Init.ClockDiv = 178;  // 400kHz for identification phase

    if (HAL_SD_Init(&g_hsd) != HAL_OK) {
        return false;
    }

    // Configure DMA2 Channel 4 for SDIO
    g_hdma_sdio.Instance = DMA2_Channel4;
    g_hdma_sdio.Init.Direction = DMA_MEMORY_TO_PERIPH;
    g_hdma_sdio.Init.PeriphInc = DMA_PINC_DISABLE;
    g_hdma_sdio.Init.MemInc = DMA_MINC_ENABLE;
    g_hdma_sdio.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    g_hdma_sdio.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    g_hdma_sdio.Init.Mode = DMA_NORMAL;
    g_hdma_sdio.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    HAL_DMA_Init(&g_hdma_sdio);
    __HAL_LINKDMA(&g_hsd, hdmatx, g_hdma_sdio);

    // Switch to 4-bit bus at 400kHz (sends ACMD6 to card + configures peripheral)
    HAL_SD_ConfigWideBusOperation(&g_hsd, SDIO_BUS_WIDE_4B);

    // Switch to transfer clock speed, preserve bus width and other CLKCR bits
    // 72MHz / (4+2) = 12MHz (matches 野火 SDIO_TRANSFER_CLK_DIV)
    SDIO->CLKCR = (SDIO->CLKCR & ~0xFFU) | 4U;
    g_hsd.Init.ClockDiv = 4;

    // Enable SDIO and DMA2 Channel 4/5 interrupts
    // Priority >= 5 (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) for FreeRTOS API safety
    HAL_NVIC_SetPriority(SDIO_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);
    HAL_NVIC_SetPriority(DMA2_Channel4_5_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA2_Channel4_5_IRQn);

    mReady = true;
    g_sd_ready = 1;
    return true;
}

bool SdCard::writeBlocks(const uint8_t* data, uint32_t blockAddr, uint32_t numBlocks) {
    return startWrite(data, blockAddr, numBlocks) && waitWrite();
}

bool SdCard::startWrite(const uint8_t* data, uint32_t blockAddr, uint32_t numBlocks) {
    if (!mReady) return false;

    // Clear any stale semaphore signal
    xSemaphoreTake(g_sd_dma_sem, 0);

    // Start DMA write (non-blocking, CPU free during transfer)
    HAL_StatusTypeDef status = HAL_SD_WriteBlocks_DMA(
        &g_hsd,
        const_cast<uint8_t*>(data),
        blockAddr,
        numBlocks
    );

    return (status == HAL_OK);
}

bool SdCard::waitWrite() {
    // Wait for DMA+SDIO completion (HAL_SD_TxCpltCallback gives semaphore)
    if (xSemaphoreTake(g_sd_dma_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return false;
    }

    // Check for errors
    if (g_hsd.ErrorCode != HAL_SD_ERROR_NONE) {
        return false;
    }

    // Wait for card to finish internal programming
    uint32_t timeout = HAL_GetTick() + 5000;
    while (HAL_SD_GetCardState(&g_hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() > timeout) return false;
    }

    return true;
}

uint32_t SdCard::getLastError() const {
    return g_hsd.ErrorCode;
}

uint32_t SdCard::getBlockCount() const {
    if (!mReady) return 0;
    HAL_SD_CardInfoTypeDef info;
    HAL_SD_GetCardInfo(const_cast<SD_HandleTypeDef*>(&g_hsd), &info);
    return info.LogBlockNbr;
}

} // namespace arcana
