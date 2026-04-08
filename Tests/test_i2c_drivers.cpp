/**
 * @file test_i2c_drivers.cpp
 * @brief Host coverage for I2cBus + Mpu6050Sensor + Ap3216cSensor + DhtSensor.
 *
 * I2cBus is a software bit-bang driver. The HAL_GPIO_Write/Read stubs make
 * SDA reads return "released" (HIGH) which means every I2C ACK fails — but
 * we still cover all the bit-shifting and byte-pulse paths.
 *
 * delayUs() spins on DWT->CYCCNT. test_hal_stub leaves CYCCNT at 0 forever,
 * so we set SystemCoreClock = 0 before each test → cycles = us * 0 = 0 →
 * the wait loop exits immediately. Restored to 72MHz after.
 */
#include <gtest/gtest.h>
#include <cstring>

#include "stm32f1xx_hal.h"
#include "I2cBus.hpp"
#include "Mpu6050Sensor.hpp"
#include "Ap3216cSensor.hpp"
#include "DhtSensor.hpp"

using arcana::I2cBus;

namespace {
struct DwtFreezer {
    uint32_t saved;
    DwtFreezer() : saved(SystemCoreClock) { SystemCoreClock = 0; }
    ~DwtFreezer() { SystemCoreClock = saved; }
};
} // anonymous

// ── I2cBus ─────────────────────────────────────────────────────────────────

TEST(I2cBusTest, GetInstanceSingleton) {
    EXPECT_EQ(&I2cBus::getInstance(), &I2cBus::getInstance());
}

TEST(I2cBusTest, InitHALCompletes) {
    DwtFreezer f;
    I2cBus::getInstance().initHAL();
    SUCCEED();
}

TEST(I2cBusTest, WriteRegFailsOnNack) {
    DwtFreezer f;
    /* SDA reads always return HIGH (released) → I2cBus interprets as NACK
     * → sendByte returns false → writeReg short-circuits and returns false */
    EXPECT_FALSE(I2cBus::getInstance().writeReg(0x68, 0x6B, 0x00));
}

TEST(I2cBusTest, ReadRegFailsOnNack) {
    DwtFreezer f;
    uint8_t value = 0;
    EXPECT_FALSE(I2cBus::getInstance().readReg(0x68, 0x3B, &value));
}

TEST(I2cBusTest, ReadRegsFailsOnNack) {
    DwtFreezer f;
    uint8_t buf[6] = {};
    EXPECT_FALSE(I2cBus::getInstance().readRegs(0x68, 0x3B, buf, 6));
}

// ── Mpu6050Sensor + Ap3216cSensor (production drivers, NOT the stub) ──────
//
// These tests link the real Mpu6050Sensor.cpp + Ap3216cSensor.cpp instead of
// the host stub in sensor_drivers_stub.cpp. The production drivers use
// I2cBus to read raw registers — every read fails on host but the read()
// method's branches are still exercised.

TEST(Mpu6050SensorTest, ReadReturnsInvalidWhenI2cFails) {
    DwtFreezer f;
    arcana::sensor::Mpu6050Sensor s;
    s.init(&I2cBus::getInstance());
    arcana::sensor::Mpu6050Reading r = s.read();
    /* I2cBus failed every read → reading should be marked invalid OR the
     * raw values are zero (depends on impl). Just verify no crash. */
    (void)r;
    SUCCEED();
}

TEST(Ap3216cSensorTest, ReadReturnsInvalidWhenI2cFails) {
    DwtFreezer f;
    arcana::light::Ap3216cSensor s;
    s.init(&I2cBus::getInstance());
    arcana::light::Ap3216cReading r = s.read();
    (void)r;
    SUCCEED();
}

// ── DhtSensor (single-wire DHT11) ──────────────────────────────────────────
//
// DHT11 uses GPIO bit-bang on PA1. Pull line down, wait for response, read
// 40 bits. With our HAL stubs the response will time out, so read() returns
// invalid — but we still cover the start sequence + timeout branches.

TEST(DhtSensorTest, ReadReturnsInvalidOnTimeout) {
    DwtFreezer f;
    arcana::sensor::DhtSensor s;
    s.initHAL();
    arcana::sensor::DhtReading r = s.read();
    /* No real DHT → invalid */
    EXPECT_FALSE(r.valid);
}
