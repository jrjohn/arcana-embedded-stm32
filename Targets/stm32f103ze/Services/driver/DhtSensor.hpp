#pragma once

#include "stm32f1xx_hal.h"
#include <cstdint>

namespace arcana {
namespace sensor {

struct DhtReading {
    float temperature;
    float humidity;
    bool valid;
};

class DhtSensor {
public:
    DhtSensor();

    void initHAL();
    DhtReading read();

private:
    static const uint16_t PIN = GPIO_PIN_6;
    static GPIO_TypeDef* const PORT;

    void enableDwtCycleCounter();
    void setOutput();
    void setInput();
    void pinLow();
    void pinHigh();
    bool pinRead();
    void delayUs(uint32_t us);
    bool waitForLevel(bool level, uint32_t timeoutUs);
};

} // namespace sensor
} // namespace arcana
