#include "Esp8266.hpp"
#include <cstring>
#include <cstdio>

// Global USART3 handle for IRQ handler
static UART_HandleTypeDef sHuart3;

// USART3 IRQ handler (C linkage)
extern "C" void USART3_IRQHandler(void) {
    arcana::Esp8266& esp = arcana::Esp8266::getInstance();

    // Check RXNE (byte received)
    if (__HAL_UART_GET_FLAG(&sHuart3, UART_FLAG_RXNE)) {
        uint8_t byte = (uint8_t)(sHuart3.Instance->DR & 0xFF);
        esp.isr_onRxByte(byte);
    }

    // Check IDLE (line idle = frame complete)
    if (__HAL_UART_GET_FLAG(&sHuart3, UART_FLAG_IDLE)) {
        // Clear IDLE flag by reading SR then DR
        volatile uint32_t tmp = sHuart3.Instance->SR;
        tmp = sHuart3.Instance->DR;
        (void)tmp;
        esp.isr_onIdle();
    }
}

namespace arcana {

Esp8266::Esp8266()
    : mRxBuf{}
    , mRxPos(0)
    , mRxLen(0)
    , mMqttBuf{}
    , mMqttLen(0)
    , mMqttReady(false)
    , mFrameSemBuf()
    , mFrameSem(0)
    , mInitialized(false)
    , mIpdPassthrough(false)
{
}

Esp8266::~Esp8266() {}

Esp8266& Esp8266::getInstance() {
    static Esp8266 sInstance;
    return sInstance;
}

void Esp8266::initGpio() {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};

    // PB10 = USART3_TX (AF push-pull)
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    // PB11 = USART3_RX (input floating)
    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    // PG13 = WIFI_CH_PD (output, default LOW = chip disabled)
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOG, &gpio);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_RESET);

    // PG14 = WIFI_RST (output, default HIGH = not in reset)
    gpio.Pin = GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOG, &gpio);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);
}

void Esp8266::initUsart() {
    sHuart3.Instance = USART3;
    sHuart3.Init.BaudRate = 115200;
    sHuart3.Init.WordLength = UART_WORDLENGTH_8B;
    sHuart3.Init.StopBits = UART_STOPBITS_1;
    sHuart3.Init.Parity = UART_PARITY_NONE;
    sHuart3.Init.Mode = UART_MODE_TX_RX;
    sHuart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    sHuart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&sHuart3);

    // Enable RXNE + IDLE interrupts
    __HAL_UART_ENABLE_IT(&sHuart3, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&sHuart3, UART_IT_IDLE);

    // NVIC priority >= 5 for FreeRTOS API safety
    HAL_NVIC_SetPriority(USART3_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

bool Esp8266::initHAL() {
    mFrameSem = xSemaphoreCreateBinaryStatic(&mFrameSemBuf);
    if (!mFrameSem) return false;

    initGpio();
    initUsart();

    mInitialized = true;
    return true;
}

bool Esp8266::speedUp(uint32_t baud) {
    // Send baud change command at current 115200
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+UART_CUR=%lu,8,1,0,0", (unsigned long)baud);
    if (!sendCmd(cmd, "OK", 2000)) return false;

    // ESP8266 switches immediately after OK — change STM32 to match
    vTaskDelay(pdMS_TO_TICKS(50));  // let ESP8266 settle

    __HAL_UART_DISABLE_IT(&sHuart3, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&sHuart3, UART_IT_IDLE);

    sHuart3.Init.BaudRate = baud;
    HAL_UART_Init(&sHuart3);

    __HAL_UART_ENABLE_IT(&sHuart3, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&sHuart3, UART_IT_IDLE);

    mRxPos = 0;
    mRxLen = 0;
    mRxBuf[0] = '\0';

    vTaskDelay(pdMS_TO_TICKS(100));

    // Verify with AT
    return sendCmd("AT", "OK", 1000);
}

void Esp8266::reset() {
    // Disable chip + hardware reset (following 野火 reference)
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_RESET);  // CH_PD = LOW
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_RESET);  // RST = LOW
    vTaskDelay(pdMS_TO_TICKS(500));

    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);    // RST = HIGH
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_SET);    // CH_PD = HIGH (enable)
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for ESP8266 boot

    // Clear any boot messages
    mRxPos = 0;
    mRxLen = 0;
    mRxBuf[0] = '\0';
}

// --- ISR callbacks (called from USART3_IRQHandler) ---

void Esp8266::isr_onRxByte(uint8_t byte) {
    if (mRxPos < RX_BUF_SIZE - 1) {
        mRxBuf[mRxPos] = (char)byte;
        mRxPos = mRxPos + 1;
        mRxBuf[mRxPos] = '\0';
    }
}

void Esp8266::isr_onIdle() {
    if (mRxPos == 0) return;

    mRxLen = mRxPos;

    // Check for unsolicited incoming data (+IPD or +MQTTSUBRECV)
    // Search anywhere in buffer — may arrive appended to AT response (no IDLE gap)
    if (!mIpdPassthrough) {
        const char* mqttPos = nullptr;
        uint16_t mqttOff = 0;

        // Search for +MQTTSUBRECV: or +IPD, anywhere in buffer
        for (uint16_t i = 0; i + 13 <= mRxLen; i++) {
            if (mRxBuf[i] == '+') {
                if (strncmp(mRxBuf + i, "+MQTTSUBRECV:", 13) == 0 ||
                    strncmp(mRxBuf + i, "+IPD,", 5) == 0) {
                    mqttPos = mRxBuf + i;
                    mqttOff = i;
                    break;
                }
            }
        }

        if (mqttPos) {
            uint16_t dataLen = mRxLen - mqttOff;
            uint16_t copyLen = dataLen < MQTT_BUF_SIZE - 1 ? dataLen : MQTT_BUF_SIZE - 1;
            memcpy(mMqttBuf, mqttPos, copyLen);
            mMqttBuf[copyLen] = '\0';
            mMqttLen = copyLen;
            mMqttReady = true;

            // Truncate mRxBuf to keep only the AT response before +MQTTSUBRECV
            // so sendCmd can still find "OK" / "SEND OK" etc.
            if (mqttOff > 0) {
                mRxBuf[mqttOff] = '\0';
                mRxLen = mqttOff;
                mRxPos = mqttOff;
            } else {
                // Entire buffer is MQTT — clear it
                mRxPos = 0;
                mRxBuf[0] = '\0';
            }
        }
    }

    // Signal that a complete frame was received
    BaseType_t woken = pdFALSE;
    if (mFrameSem) {
        xSemaphoreGiveFromISR(mFrameSem, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

// --- AT command interface ---

bool Esp8266::sendCmd(const char* cmd, const char* expect, uint32_t timeoutMs) {
    // Clear RX buffer
    mRxPos = 0;
    mRxLen = 0;
    mRxBuf[0] = '\0';
    // Drain any stale semaphore signals
    xSemaphoreTake(mFrameSem, 0);

    // Transmit command + CRLF
    HAL_UART_Transmit(&sHuart3, (uint8_t*)cmd, strlen(cmd), 1000);
    uint8_t crlf[] = {'\r', '\n'};
    HAL_UART_Transmit(&sHuart3, crlf, 2, 100);

    // Wait for frames until we see expected response or timeout
    uint32_t start = xTaskGetTickCount();
    uint32_t remaining = pdMS_TO_TICKS(timeoutMs);

    while (remaining > 0) {
        if (xSemaphoreTake(mFrameSem, remaining) == pdTRUE) {
            if (responseContains(expect)) {
                return true;
            }
            if (responseContains("ERROR") || responseContains("FAIL")) {
                return false;
            }
            // More data may come - keep waiting
            uint32_t elapsed = xTaskGetTickCount() - start;
            uint32_t totalTicks = pdMS_TO_TICKS(timeoutMs);
            remaining = elapsed < totalTicks ? totalTicks - elapsed : 0;
        } else {
            break;  // Timeout
        }
    }

    // Final check in case data arrived just before timeout
    return responseContains(expect);
}

bool Esp8266::sendData(const uint8_t* data, uint16_t len, uint32_t timeoutMs) {
    return HAL_UART_Transmit(&sHuart3, const_cast<uint8_t*>(data),
                              len, timeoutMs) == HAL_OK;
}

bool Esp8266::responseContains(const char* str) const {
    if (mRxPos == 0 || !str) return false;
    uint16_t slen = strlen(str);
    if (slen > mRxPos) return false;
    // Use memcmp instead of strstr to handle embedded null bytes
    // (binary MQTT data echoed by ESP8266 contains 0x00)
    for (uint16_t i = 0; i <= mRxPos - slen; i++) {
        if (memcmp(mRxBuf + i, str, slen) == 0) return true;
    }
    return false;
}

void Esp8266::clearRx() {
    mRxPos = 0;
    mRxLen = 0;
    mRxBuf[0] = '\0';
    xSemaphoreTake(mFrameSem, 0);  // drain stale signal
}

bool Esp8266::waitFor(const char* expect, uint32_t timeoutMs) {
    uint32_t start = xTaskGetTickCount();
    uint32_t remaining = pdMS_TO_TICKS(timeoutMs);

    while (remaining > 0) {
        if (xSemaphoreTake(mFrameSem, remaining) == pdTRUE) {
            if (responseContains(expect)) {
                return true;
            }
            if (responseContains("ERROR") || responseContains("FAIL")) {
                return false;
            }
            uint32_t elapsed = xTaskGetTickCount() - start;
            uint32_t totalTicks = pdMS_TO_TICKS(timeoutMs);
            remaining = elapsed < totalTicks ? totalTicks - elapsed : 0;
        } else {
            break;
        }
    }
    return responseContains(expect);
}

} // namespace arcana
