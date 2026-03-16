#include "Hc08Ble.hpp"
#include <cstring>
#include <cstdio>

// Global USART2 handle for IRQ handler
static UART_HandleTypeDef sHuart2;

// USART2 IRQ handler (C linkage)
extern "C" void USART2_IRQHandler(void) {
    uint32_t sr = sHuart2.Instance->SR;

    // RXNE — byte received
    if (sr & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)(sHuart2.Instance->DR & 0xFF);
        arcana::Hc08Ble::getInstance().isr_onRxByte(byte);
    }

    // IDLE — line idle after reception (frame complete)
    if (sr & USART_SR_IDLE) {
        (void)sHuart2.Instance->DR;  // clear IDLE flag by reading DR
        arcana::Hc08Ble::getInstance().isr_onIdle();
    }
}

namespace arcana {

Hc08Ble::Hc08Ble()
    : mRxBuf{}
    , mRxLen(0)
    , mFrameSem(0)
    , mFrameSemBuf()
    , mConnected(false)
{
}

Hc08Ble::~Hc08Ble() {}

Hc08Ble& Hc08Ble::getInstance() {
    static Hc08Ble sInstance;
    return sInstance;
}

void Hc08Ble::initGpio() {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};

    // PA2 = USART2_TX (AF push-pull)
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    // PA3 = USART2_RX (input floating)
    gpio.Pin = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
}

void Hc08Ble::initUsart() {
    sHuart2.Instance = USART2;
    sHuart2.Init.BaudRate = 9600;
    sHuart2.Init.WordLength = UART_WORDLENGTH_8B;
    sHuart2.Init.StopBits = UART_STOPBITS_1;
    sHuart2.Init.Parity = UART_PARITY_NONE;
    sHuart2.Init.Mode = UART_MODE_TX_RX;
    sHuart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    sHuart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&sHuart2);

    // Enable RXNE + IDLE interrupts
    __HAL_UART_ENABLE_IT(&sHuart2, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&sHuart2, UART_IT_IDLE);

    HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

bool Hc08Ble::initHAL() {
    mFrameSem = xSemaphoreCreateBinaryStatic(&mFrameSemBuf);
    if (!mFrameSem) return false;

    initGpio();
    initUsart();

    printf("[BLE] HC-08 USART2 @ 9600 init\r\n");

    // Quick AT test
    if (sendCmd("AT")) {
        printf("[BLE] HC-08 ready\r\n");
        return true;
    }

    printf("[BLE] HC-08 no response\r\n");
    return false;
}

// ---------------------------------------------------------------------------
// ISR callbacks
// ---------------------------------------------------------------------------

void Hc08Ble::isr_onRxByte(uint8_t byte) {
    if (mRxLen < RX_BUF_SIZE - 1) {
        mRxBuf[mRxLen++] = (char)byte;
        mRxBuf[mRxLen] = '\0';
    }
}

void Hc08Ble::isr_onIdle() {
    BaseType_t woken = pdFALSE;
    if (mFrameSem) {
        xSemaphoreGiveFromISR(mFrameSem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

// ---------------------------------------------------------------------------
// AT commands + data send
// ---------------------------------------------------------------------------

bool Hc08Ble::sendCmd(const char* cmd, const char* expect, uint32_t timeoutMs) {
    // Clear RX buffer
    mRxLen = 0;
    mRxBuf[0] = '\0';
    xSemaphoreTake(mFrameSem, 0);  // clear any stale signal

    // Transmit command
    HAL_UART_Transmit(&sHuart2, (uint8_t*)cmd, strlen(cmd), 100);

    // Wait for response frame (IDLE line = end of response)
    if (xSemaphoreTake(mFrameSem, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;  // timeout
    }

    // Check for expected string
    if (expect && expect[0]) {
        return strstr(mRxBuf, expect) != nullptr;
    }
    return mRxLen > 0;
}

uint16_t Hc08Ble::waitForData(uint32_t timeoutMs) {
    if (xSemaphoreTake(mFrameSem, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
        return mRxLen;
    }
    return 0;
}

bool Hc08Ble::send(const uint8_t* data, uint16_t len, uint32_t timeoutMs) {
    HAL_StatusTypeDef st = HAL_UART_Transmit(&sHuart2, (uint8_t*)data, len, timeoutMs);
    return st == HAL_OK;
}

} // namespace arcana
