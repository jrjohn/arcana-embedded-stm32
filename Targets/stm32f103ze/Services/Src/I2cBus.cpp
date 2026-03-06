#include "I2cBus.hpp"

namespace arcana {

// Software I2C on PB6 (SCL) / PB7 (SDA)
// Both configured as open-drain output, external 10K pull-ups on board

static GPIO_TypeDef* const PORT = GPIOB;
static const uint16_t SCL_PIN = GPIO_PIN_6;
static const uint16_t SDA_PIN = GPIO_PIN_7;

I2cBus::I2cBus() {}

I2cBus& I2cBus::getInstance() {
    static I2cBus sInstance;
    return sInstance;
}

void I2cBus::initHAL() {
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Enable DWT cycle counter for microsecond delays
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    GPIO_InitTypeDef gpio = {};
    gpio.Pin = SCL_PIN | SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PORT, &gpio);

    // Idle state: both lines HIGH
    sclHigh();
    sdaHigh();
    delayUs(10);
}

void I2cBus::sclHigh() { HAL_GPIO_WritePin(PORT, SCL_PIN, GPIO_PIN_SET); }
void I2cBus::sclLow()  { HAL_GPIO_WritePin(PORT, SCL_PIN, GPIO_PIN_RESET); }
void I2cBus::sdaHigh() { HAL_GPIO_WritePin(PORT, SDA_PIN, GPIO_PIN_SET); }
void I2cBus::sdaLow()  { HAL_GPIO_WritePin(PORT, SDA_PIN, GPIO_PIN_RESET); }
bool I2cBus::sdaRead() { return HAL_GPIO_ReadPin(PORT, SDA_PIN) == GPIO_PIN_SET; }

void I2cBus::delayUs(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles) {}
}

void I2cBus::start() {
    sdaHigh(); delayUs(5);
    sclHigh(); delayUs(5);
    sdaLow();  delayUs(5);
    sclLow();  delayUs(5);
}

void I2cBus::stop() {
    sdaLow();  delayUs(5);
    sclHigh(); delayUs(5);
    sdaHigh(); delayUs(5);
}

bool I2cBus::sendByte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) sdaHigh(); else sdaLow();
        delayUs(2);
        sclHigh(); delayUs(5);
        sclLow();  delayUs(5);
    }
    // Read ACK
    sdaHigh(); delayUs(2);
    sclHigh(); delayUs(5);
    bool ack = !sdaRead();
    sclLow();  delayUs(5);
    return ack;
}

uint8_t I2cBus::recvByte(bool ack) {
    uint8_t byte = 0;
    sdaHigh(); // Release SDA for reading
    for (int i = 7; i >= 0; i--) {
        sclHigh(); delayUs(5);
        if (sdaRead()) byte |= (1 << i);
        sclLow();  delayUs(5);
    }
    // Send ACK/NACK
    if (ack) sdaLow(); else sdaHigh();
    delayUs(2);
    sclHigh(); delayUs(5);
    sclLow();  delayUs(5);
    sdaHigh();
    return byte;
}

bool I2cBus::writeReg(uint8_t devAddr, uint8_t reg, uint8_t value) {
    start();
    if (!sendByte(devAddr << 1)) { stop(); return false; }
    if (!sendByte(reg))          { stop(); return false; }
    if (!sendByte(value))        { stop(); return false; }
    stop();
    return true;
}

bool I2cBus::readReg(uint8_t devAddr, uint8_t reg, uint8_t* value) {
    return readRegs(devAddr, reg, value, 1);
}

bool I2cBus::readRegs(uint8_t devAddr, uint8_t reg, uint8_t* buf, uint16_t len) {
    // Write register address
    start();
    if (!sendByte(devAddr << 1)) { stop(); return false; }
    if (!sendByte(reg))          { stop(); return false; }
    // Repeated start + read
    start();
    if (!sendByte((devAddr << 1) | 1)) { stop(); return false; }
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = recvByte(i < len - 1); // ACK all except last
    }
    stop();
    return true;
}

} // namespace arcana
