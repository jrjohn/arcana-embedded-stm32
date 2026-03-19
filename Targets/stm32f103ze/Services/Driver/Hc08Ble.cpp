#include "Hc08Ble.hpp"
#include "Log.hpp"
#include "EventCodes.hpp"
#include <cstring>

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
    : mAtBuf{}
    , mAtLen(0)
    , mRingBuf{}
    , mRingWr(0)
    , mRingRd(0)
    , mAssembler()
    , mRxSem(0)
    , mRxSemBuf()
    , mDataMode(false)
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
    mRxSem = xSemaphoreCreateBinaryStatic(&mRxSemBuf);
    if (!mRxSem) return false;

    initGpio();
    initUsart();

    LOG_I(ats::ErrorSource::Ble, evt::BLE_HC08_INIT);

    // Quick AT test (only works when NOT connected to a BLE peer)
    if (sendCmd("AT")) {
        LOG_I(ats::ErrorSource::Ble, evt::BLE_HC08_AT_OK);

        // Query firmware version (response is version string, not "OK")
        sendCmd("AT+VERSION", "", 500);

        // Set device name
        sendCmd("AT+NAME=ArcanaBLE", "OK");
        LOG_I(ats::ErrorSource::Ble, evt::BLE_HC08_NAME);

        return true;
    }

    // If connected to BLE peer, AT commands don't work — still OK for data mode
    LOG_I(ats::ErrorSource::Ble, evt::BLE_HC08_DATA_MODE);
    return true;  // not a failure — transparent mode works
}

// ---------------------------------------------------------------------------
// ISR callbacks
// ---------------------------------------------------------------------------

void Hc08Ble::isr_onRxByte(uint8_t byte) {
    if (mDataMode) {
        // Data mode: push into ring buffer
        uint16_t next = (mRingWr + 1) & (RX_RING_SIZE - 1);
        if (next != mRingRd) {
            mRingBuf[mRingWr] = byte;
            mRingWr = next;
        }
        // Full → drop byte (backpressure)
    } else {
        // AT mode: linear buffer
        if (mAtLen < AT_BUF_SIZE - 1) {
            mAtBuf[mAtLen] = byte;
            mAtLen = mAtLen + 1;
            mAtBuf[mAtLen] = '\0';
        }
    }
}

void Hc08Ble::isr_onIdle() {
    BaseType_t woken = pdFALSE;
    if (mRxSem) {
        xSemaphoreGiveFromISR(mRxSem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

// ---------------------------------------------------------------------------
// Ring buffer → FrameAssembler
// ---------------------------------------------------------------------------

bool Hc08Ble::processRxRing() {
    while (mRingRd != mRingWr) {
        uint8_t b = mRingBuf[mRingRd];
        mRingRd = (mRingRd + 1) & (RX_RING_SIZE - 1);

        if (mAssembler.feedByte(b)) {
            return true;  // complete frame ready — caller calls getFrame()
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// AT commands + data send
// ---------------------------------------------------------------------------

bool Hc08Ble::sendCmd(const char* cmd, const char* expect, uint32_t timeoutMs) {
    mAtLen = 0;
    mAtBuf[0] = '\0';
    xSemaphoreTake(mRxSem, 0);  // clear stale

    HAL_UART_Transmit(&sHuart2, (uint8_t*)cmd, strlen(cmd), 100);

    if (xSemaphoreTake(mRxSem, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;
    }

    if (expect && expect[0]) {
        return strstr((const char*)mAtBuf, expect) != nullptr;
    }
    return mAtLen > 0;
}

uint16_t Hc08Ble::waitForData(uint32_t timeoutMs) {
    if (xSemaphoreTake(mRxSem, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
        if (mDataMode) {
            // Return ring buffer pending count
            return (mRingWr - mRingRd) & (RX_RING_SIZE - 1);
        }
        return mAtLen;
    }
    return 0;
}

bool Hc08Ble::send(const uint8_t* data, uint16_t len, uint32_t timeoutMs) {
    HAL_StatusTypeDef st = HAL_UART_Transmit(&sHuart2, (uint8_t*)data, len, timeoutMs);
    return st == HAL_OK;
}

} // namespace arcana
