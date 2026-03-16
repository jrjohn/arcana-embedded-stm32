#include "DhtSensor.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace sensor {

GPIO_TypeDef* const DhtSensor::PORT = GPIOD;

DhtSensor::DhtSensor() {}

void DhtSensor::enableDwtCycleCounter() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void DhtSensor::initHAL() {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    enableDwtCycleCounter();
    setInput();
}

void DhtSensor::setOutput() {
    GPIO_InitTypeDef gpio = {};
    gpio.Pin = PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PORT, &gpio);
}

void DhtSensor::setInput() {
    GPIO_InitTypeDef gpio = {};
    gpio.Pin = PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(PORT, &gpio);
}

void DhtSensor::pinLow() {
    HAL_GPIO_WritePin(PORT, PIN, GPIO_PIN_RESET);
}

void DhtSensor::pinHigh() {
    HAL_GPIO_WritePin(PORT, PIN, GPIO_PIN_SET);
}

bool DhtSensor::pinRead() {
    return HAL_GPIO_ReadPin(PORT, PIN) == GPIO_PIN_SET;
}

void DhtSensor::delayUs(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles) {}
}

bool DhtSensor::waitForLevel(bool level, uint32_t timeoutUs) {
    uint32_t start = DWT->CYCCNT;
    uint32_t timeout = timeoutUs * (SystemCoreClock / 1000000);
    while (pinRead() != level) {
        if ((DWT->CYCCNT - start) >= timeout) return false;
    }
    return true;
}

DhtReading DhtSensor::read() {
    DhtReading result = {0.0f, 0.0f, false};
    uint8_t data[5] = {};

    // Enter critical section (timing-sensitive)
    taskENTER_CRITICAL();

    // 1. Send start signal: pull LOW >= 18ms
    setOutput();
    pinLow();
    delayUs(20000);

    // 2. Release line (pull-up takes it HIGH)
    pinHigh();
    setInput();
    delayUs(30);

    // 3. Wait for DHT response: LOW ~80us
    if (!waitForLevel(false, 100)) goto done;
    if (!waitForLevel(true, 100)) goto done;

    // 4. Wait for end of HIGH response ~80us
    if (!waitForLevel(false, 100)) goto done;

    // 5. Read 40 bits (5 bytes)
    for (uint8_t i = 0; i < 40; i++) {
        // Each bit starts with ~50us LOW
        if (!waitForLevel(true, 80)) goto done;

        // Measure HIGH duration: ~26us = 0, ~70us = 1
        uint32_t start = DWT->CYCCNT;
        if (!waitForLevel(false, 100)) goto done;
        uint32_t elapsed = DWT->CYCCNT - start;
        uint32_t thresholdCycles = 40 * (SystemCoreClock / 1000000);

        uint8_t byteIdx = i / 8;
        data[byteIdx] <<= 1;
        if (elapsed > thresholdCycles) {
            data[byteIdx] |= 1;
        }
    }

    // Exit critical section
    taskEXIT_CRITICAL();

    // 6. Verify checksum
    {
        uint8_t checksum = data[0] + data[1] + data[2] + data[3];
        if (checksum != data[4]) return result;
    }

    // 7. Parse DHT11 format (integer only)
    result.humidity = static_cast<float>(data[0]);
    result.temperature = static_cast<float>(data[2]);
    if (data[3] & 0x80) {
        result.temperature = -result.temperature;
    }
    result.valid = true;
    return result;

done:
    taskEXIT_CRITICAL();
    return result;
}

} // namespace sensor
} // namespace arcana
