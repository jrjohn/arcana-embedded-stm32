#pragma once

#include "I2cBus.hpp"
#include <cstdint>

namespace arcana {
namespace light {

struct Ap3216cReading {
    uint16_t ambientLight;
    uint16_t proximity;
    bool valid;
};

class Ap3216cSensor {
public:
    Ap3216cSensor();
    void init(I2cBus* bus);
    Ap3216cReading read();

private:
    static const uint8_t ADDR = 0x1E;
    static const uint8_t REG_SYSTEM_CONFIG = 0x00;
    static const uint8_t REG_ALS_DATA_LOW = 0x0C;
    static const uint8_t REG_ALS_DATA_HIGH = 0x0D;
    static const uint8_t REG_PS_DATA_LOW = 0x0E;
    static const uint8_t REG_PS_DATA_HIGH = 0x0F;

    I2cBus* mBus;
};

} // namespace light
} // namespace arcana
