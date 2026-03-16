#pragma once

#include "LedService.hpp"
#include "stm32f1xx_hal.h"

namespace arcana {
namespace led {

class LedServiceImpl : public LedService {
public:
    static LedService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

private:
    LedServiceImpl();
    ~LedServiceImpl();
    LedServiceImpl(const LedServiceImpl&);
    LedServiceImpl& operator=(const LedServiceImpl&);

    static void onTimerTick(TimerModel* model, void* context);
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void allOff();

    // Pin definitions (active low, common anode)
    static const uint16_t PIN_R = GPIO_PIN_5;
    static const uint16_t PIN_G = GPIO_PIN_0;
    static const uint16_t PIN_B = GPIO_PIN_1;

    uint8_t mColorIndex;
    bool mRunning;
};

} // namespace led
} // namespace arcana
