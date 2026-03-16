#pragma once

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <cstdint>

namespace arcana {

/**
 * HC-08 BLE 4.0 driver — USART2 (PA2=TX, PA3=RX) @ 9600 baud.
 *
 * Transparent UART-BLE bridge. AT commands when disconnected,
 * raw data passthrough when connected.
 *
 * Usage:
 *   Hc08Ble& ble = Hc08Ble::getInstance();
 *   ble.initHAL();
 *   ble.sendCmd("AT+NAMEArcana");  // set BLE name
 *   ble.send(data, len);           // send data to BLE peer
 */
class Hc08Ble {
public:
    static Hc08Ble& getInstance();

    bool initHAL();

    /** Send AT command (only works when not connected). Returns true if 'expect' found. */
    bool sendCmd(const char* cmd, const char* expect = "OK", uint32_t timeoutMs = 500);

    /** Send raw data bytes to BLE peer */
    bool send(const uint8_t* data, uint16_t len, uint32_t timeoutMs = 200);

    /** Get last response buffer */
    const char* getResponse() const { return mRxBuf; }
    uint16_t getResponseLen() const { return mRxLen; }

    /** Check if BLE peer is connected (based on STATE pin or AT response) */
    bool isConnected() const { return mConnected; }

    /** Called from USART2 IRQ — byte received */
    void isr_onRxByte(uint8_t byte);

    /** Called from USART2 IRQ — idle line (frame complete) */
    void isr_onIdle();

private:
    Hc08Ble();
    ~Hc08Ble();
    Hc08Ble(const Hc08Ble&);
    Hc08Ble& operator=(const Hc08Ble&);

    void initGpio();
    void initUsart();

    // RX buffer
    static const uint16_t RX_BUF_SIZE = 128;
    char mRxBuf[RX_BUF_SIZE];
    volatile uint16_t mRxLen;

    // Frame-complete semaphore (ISR → task)
    SemaphoreHandle_t mFrameSem;
    StaticSemaphore_t mFrameSemBuf;

    bool mConnected;
};

} // namespace arcana
