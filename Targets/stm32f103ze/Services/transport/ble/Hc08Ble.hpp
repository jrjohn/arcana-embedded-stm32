#pragma once

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include "FrameAssembler.hpp"
#include <cstdint>

namespace arcana {

/**
 * HC-08 BLE 4.0 driver — USART2 (PA2=TX, PA3=RX) @ 9600 baud.
 *
 * ISR accumulates bytes into a ring buffer.
 * processRxRing() feeds bytes to FrameAssembler and enqueues complete frames.
 */

/** A complete reassembled frame ready for processing */
struct FrameItem {
    static constexpr uint16_t MAX_DATA = 64;
    uint8_t data[MAX_DATA];
    uint16_t len;
    enum Transport : uint8_t { BLE = 0, MQTT = 1 } source;
};

class Hc08Ble {
public:
    static Hc08Ble& getInstance();

    bool initHAL();

    /** Send AT command (only works when not connected). Returns true if 'expect' found. */
    bool sendCmd(const char* cmd, const char* expect = "OK", uint32_t timeoutMs = 500);

    /** Send raw data bytes to BLE peer */
    bool send(const uint8_t* data, uint16_t len, uint32_t timeoutMs = 200);

    /** Get last response buffer (AT mode only) */
    const char* getResponse() const { return reinterpret_cast<const char*>(mAtBuf); }
    uint16_t getResponseLen() const { return mAtLen; }

    /** Clear AT response buffer */
    void clearRx() { mAtLen = 0; mAtBuf[0] = '\0'; }

    /** Wait for AT response (blocks until IDLE or timeout). Returns bytes received. */
    uint16_t waitForData(uint32_t timeoutMs);

    /**
     * Process ring buffer bytes through FrameAssembler.
     * @return true if a complete frame is ready (call getFrame/getFrameLen/resetFrame)
     */
    bool processRxRing();

    /** Get assembled frame data (valid after processRxRing returns true) */
    const uint8_t* getFrame() const { return mAssembler.getFrame(); }
    uint16_t getFrameLen() const { return mAssembler.getFrameLen(); }
    void resetFrame() { mAssembler.reset(); }

    /** Called from USART2 IRQ — byte received */
    void isr_onRxByte(uint8_t byte);

    /** Called from USART2 IRQ — idle line */
    void isr_onIdle();

    /** Set data mode (false = AT mode, true = frame reassembly mode) */
    void setDataMode(bool dataMode) { mDataMode = dataMode; }
    bool isDataMode() const { return mDataMode; }

private:
    Hc08Ble();
    ~Hc08Ble();
    Hc08Ble(const Hc08Ble&);
    Hc08Ble& operator=(const Hc08Ble&);

    void initGpio();
    void initUsart();

    // AT mode buffer (used during init)
    static const uint16_t AT_BUF_SIZE = 64;
    uint8_t mAtBuf[AT_BUF_SIZE];
    volatile uint16_t mAtLen;

    // Ring buffer for data mode (ISR writes, task reads)
    static const uint16_t RX_RING_SIZE = 128;  // power of 2
    uint8_t mRingBuf[RX_RING_SIZE];
    volatile uint16_t mRingWr;
    uint16_t mRingRd;

    // Frame reassembly
    FrameAssembler mAssembler;

    // Semaphore — ISR signals new data available
    SemaphoreHandle_t mRxSem;
    StaticSemaphore_t mRxSemBuf;

    bool mDataMode;    // false = AT, true = frame reassembly
    bool mConnected;
};

} // namespace arcana
