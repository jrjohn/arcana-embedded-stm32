#pragma once

#include "I2cBus.hpp"
#include <cstdint>

namespace arcana {
namespace sensor {

struct Mpu6050Reading {
    float temperature;
    int16_t accelX, accelY, accelZ;
    bool valid;
};

class Mpu6050Sensor {
public:
    Mpu6050Sensor();
    void init(I2cBus* bus);
    Mpu6050Reading read();

private:
    static const uint8_t ADDR = 0x68;
    static const uint8_t REG_PWR_MGMT_1 = 0x6B;
    static const uint8_t REG_ACCEL_XOUT_H = 0x3B;

    I2cBus* mBus;
};

} // namespace sensor
} // namespace arcana
