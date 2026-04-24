#pragma once

#include "stm32f1xx_hal.h"
#include <cstdint>

namespace arcana {

class I2cBus {
public:
    static I2cBus& getInstance();
    void initHAL();

    bool writeReg(uint8_t devAddr, uint8_t reg, uint8_t value);
    bool readReg(uint8_t devAddr, uint8_t reg, uint8_t* value);
    bool readRegs(uint8_t devAddr, uint8_t reg, uint8_t* buf, uint16_t len);

private:
    I2cBus();

    void sclHigh();
    void sclLow();
    void sdaHigh();
    void sdaLow();
    bool sdaRead();
    void delayUs(uint32_t us);

    void start();
    void stop();
    bool sendByte(uint8_t byte);
    uint8_t recvByte(bool ack);
};

} // namespace arcana
