#include "Ap3216cSensor.hpp"

namespace arcana {
namespace light {

Ap3216cSensor::Ap3216cSensor() : mBus(0) {}

void Ap3216cSensor::init(I2cBus* bus) {
    mBus = bus;
    // Reset
    mBus->writeReg(ADDR, REG_SYSTEM_CONFIG, 0x04);
    // Wait for reset (~10ms)
    { volatile uint32_t i = 0; while (i < 100000) { i = i + 1; } }
    // Enable ALS + PS mode
    mBus->writeReg(ADDR, REG_SYSTEM_CONFIG, 0x03);
}

Ap3216cReading Ap3216cSensor::read() {
    Ap3216cReading result = {0, 0, false};
    if (!mBus) return result;

    uint8_t alsLow, alsHigh, psLow, psHigh;
    if (!mBus->readReg(ADDR, REG_ALS_DATA_LOW, &alsLow))   return result;
    if (!mBus->readReg(ADDR, REG_ALS_DATA_HIGH, &alsHigh)) return result;
    if (!mBus->readReg(ADDR, REG_PS_DATA_LOW, &psLow))     return result;
    if (!mBus->readReg(ADDR, REG_PS_DATA_HIGH, &psHigh))   return result;

    result.ambientLight = (alsHigh << 8) | alsLow;

    // PS data: psHigh[5:0] << 4 | psLow[3:0], bit6 of psLow = IR overflow
    if (psLow & 0x40) {
        result.proximity = 0;
    } else {
        result.proximity = ((psHigh & 0x3F) << 4) | (psLow & 0x0F);
    }

    result.valid = true;
    return result;
}

} // namespace light
} // namespace arcana
