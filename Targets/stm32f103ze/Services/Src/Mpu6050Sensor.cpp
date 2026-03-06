#include "Mpu6050Sensor.hpp"

namespace arcana {
namespace sensor {

Mpu6050Sensor::Mpu6050Sensor() : mBus(0) {}

void Mpu6050Sensor::init(I2cBus* bus) {
    mBus = bus;
    // Wake up MPU6050 (clear sleep bit)
    mBus->writeReg(ADDR, REG_PWR_MGMT_1, 0x00);
}

Mpu6050Reading Mpu6050Sensor::read() {
    Mpu6050Reading result = {0.0f, 0, 0, 0, false};
    if (!mBus) return result;

    // Read accel(6) + temp(2) = 8 bytes starting from 0x3B
    uint8_t buf[8];
    if (!mBus->readRegs(ADDR, REG_ACCEL_XOUT_H, buf, 8)) return result;

    result.accelX = static_cast<int16_t>((buf[0] << 8) | buf[1]);
    result.accelY = static_cast<int16_t>((buf[2] << 8) | buf[3]);
    result.accelZ = static_cast<int16_t>((buf[4] << 8) | buf[5]);

    int16_t rawTemp = static_cast<int16_t>((buf[6] << 8) | buf[7]);
    result.temperature = (rawTemp / 340.0f) + 36.53f;

    result.valid = true;
    return result;
}

} // namespace sensor
} // namespace arcana
