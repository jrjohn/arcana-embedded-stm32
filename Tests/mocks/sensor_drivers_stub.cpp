/**
 * @file sensor_drivers_stub.cpp
 * @brief Host stubs for I2cBus + Mpu6050Sensor + Ap3216cSensor.
 *
 * Real drivers bit-bang I2C against PB6/PB7 — pure HW. The stubs provide
 * just enough symbols (singleton + init + read) for SensorServiceImpl /
 * LightServiceImpl to compile and run their lifecycle paths against host.
 * read() always returns invalid=true so the publish branch in the service
 * task body is also exercised when called directly.
 */
#include "I2cBus.hpp"
#include "Mpu6050Sensor.hpp"
#include "Ap3216cSensor.hpp"

namespace arcana {

I2cBus::I2cBus() {}
I2cBus& I2cBus::getInstance() {
    static I2cBus s;
    return s;
}
void I2cBus::initHAL() {}
bool I2cBus::writeReg(uint8_t, uint8_t, uint8_t) { return true; }
bool I2cBus::readReg(uint8_t, uint8_t, uint8_t* v) { if (v) *v = 0; return true; }
bool I2cBus::readRegs(uint8_t, uint8_t, uint8_t* buf, uint16_t len) {
    for (uint16_t i = 0; i < len; ++i) buf[i] = 0;
    return true;
}
void I2cBus::sclHigh() {}
void I2cBus::sclLow()  {}
void I2cBus::sdaHigh() {}
void I2cBus::sdaLow()  {}
bool I2cBus::sdaRead() { return true; }
void I2cBus::delayUs(uint32_t) {}
void I2cBus::start() {}
void I2cBus::stop()  {}
bool I2cBus::sendByte(uint8_t) { return true; }
uint8_t I2cBus::recvByte(bool) { return 0; }

namespace sensor {
Mpu6050Sensor::Mpu6050Sensor() : mBus(nullptr) {}
void Mpu6050Sensor::init(I2cBus* bus) { mBus = bus; }
Mpu6050Reading Mpu6050Sensor::read() {
    Mpu6050Reading r{};
    r.valid = true;
    r.temperature = 25.0f;
    return r;
}
} // namespace sensor

namespace light {
Ap3216cSensor::Ap3216cSensor() : mBus(nullptr) {}
void Ap3216cSensor::init(I2cBus* bus) { mBus = bus; }
Ap3216cReading Ap3216cSensor::read() {
    Ap3216cReading r{};
    r.valid = true;
    r.ambientLight = 100;
    r.proximity    = 5;
    return r;
}
} // namespace light

} // namespace arcana
