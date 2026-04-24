#pragma once

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <cstdint>

namespace arcana {

class Esp8266 {
public:
    static Esp8266& getInstance();

    bool initHAL();
    void reset();

    /** Upgrade UART baud rate after reset (sends AT+UART_CUR to ESP8266) */
    bool speedUp(uint32_t baud = 460800);

    /** Change STM32 UART baud only (no AT command — for baud mismatch recovery) */
    void setBaud(uint32_t baud);

    // Send AT command and wait for expected response (default "OK")
    bool sendCmd(const char* cmd, const char* expect = "OK",
                 uint32_t timeoutMs = 2000);

    // Send raw data bytes
    bool sendData(const uint8_t* data, uint16_t len, uint32_t timeoutMs = 1000);

    // Get response buffer after sendCmd
    const char* getResponse() const { return mRxBuf; }
    uint16_t getResponseLen() const { return mRxLen; }

    // Check if response contains a substring
    bool responseContains(const char* str) const;

    // Wait for expected string in response (no command sent)
    bool waitFor(const char* expect, uint32_t timeoutMs);

    // Clear RX buffer (for fresh response after raw data send)
    void clearRx();

    // Called from USART3 IRQ handler - byte received
    void isr_onRxByte(uint8_t byte);
    // Called from USART3 IRQ handler - idle line detected (frame complete)
    void isr_onIdle();

    // Unsolicited MQTT subscription message buffer
    bool hasMqttMsg() const { return mMqttReady; }
    const char* getMqttMsg() const { return mMqttBuf; }
    uint16_t getMqttMsgLen() const { return mMqttLen; }
    void clearMqttMsg() { mMqttReady = false; }

    // IPD passthrough mode: +IPD stays in mRxBuf instead of mMqttBuf
    // Used by OTA download to handle large +IPD chunks
    void setIpdPassthrough(bool enable) { mIpdPassthrough = enable; }
    bool isIpdPassthrough() const { return mIpdPassthrough; }

    // --- Resource lock: cooperative ESP8266 sharing ---
    enum class User : uint8_t { None, Mqtt, Upload };

    /** Block until ESP8266 access granted. */
    void requestAccess(User who);
    /** Non-blocking: signal that a user wants access (MQTT checks and yields). */
    void requestAccessAsync(User who) { mRequestedUser = who; }
    /** Release ESP8266 for other users. */
    void releaseAccess();
    /** MQTT polls this — true means another user wants the ESP8266. */
    bool isAccessRequested() const { return mRequestedUser != User::None && mRequestedUser != mCurrentUser; }
    /** Clear request after handling. */
    void clearRequest() { mRequestedUser = User::None; }

    static const uint16_t RX_BUF_SIZE = 512;
    static const uint16_t MQTT_BUF_SIZE = 256;

private:
    Esp8266();
    ~Esp8266();

    void initGpio();
    void initUsart();

    char mRxBuf[RX_BUF_SIZE];
    volatile uint16_t mRxPos;    // Current write position in mRxBuf
    volatile uint16_t mRxLen;    // Length of last complete frame

    char mMqttBuf[MQTT_BUF_SIZE];
    volatile uint16_t mMqttLen;
    volatile bool mMqttReady;

    StaticSemaphore_t mFrameSemBuf;
    SemaphoreHandle_t mFrameSem;  // Signaled when complete frame received

    bool mInitialized;
    volatile bool mIpdPassthrough;  // When true, +IPD stays in mRxBuf

    // Resource lock state
    StaticSemaphore_t mAccessSemBuf;
    SemaphoreHandle_t mAccessSem;
    volatile User mCurrentUser;
    volatile User mRequestedUser;
};

} // namespace arcana
