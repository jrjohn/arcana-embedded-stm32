/**
 * @file Esp8266.hpp (host test stub)
 *
 * Programmable mock for the ESP8266 driver. Tests queue canned responses
 * via `test_esp_push_response(...)` and inspect what the code-under-test
 * sent via `test_esp_get_sent(...)`.
 *
 * Surface matches the real Esp8266.hpp closely enough that
 * RegistrationServiceImpl::httpRegister and other AT-command consumers
 * compile and run on host without modification.
 */
#pragma once

/* Production Esp8266.hpp includes stm32f1xx_hal.h + FreeRTOS.h + semphr.h
 * transitively, so any consumer of this mock implicitly gets UID_BASE,
 * SysTick, DWT, ADC1 plus pdMS_TO_TICKS / vTaskDelay too. */
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace arcana {

class Esp8266 {
public:
    static Esp8266& getInstance();

    bool initHAL() { return true; }
    void reset()   {}
    bool speedUp(uint32_t /*baud*/ = 460800) { return true; }
    void setBaud(uint32_t /*baud*/) {}

    bool sendCmd(const char* cmd, const char* expect = "OK",
                 uint32_t /*timeoutMs*/ = 2000) {
        mSentCmds.emplace_back(cmd);
        loadNextResponse();
        return responseContains(expect);
    }

    bool sendData(const uint8_t* data, uint16_t len, uint32_t /*timeoutMs*/ = 1000) {
        mSentData.emplace_back(data, data + len);
        loadNextResponse();
        return true;
    }

    const char* getResponse() const { return mRxBuf; }
    uint16_t    getResponseLen() const { return mRxLen; }

    bool responseContains(const char* str) const {
        if (!str || !*str) return true;
        return std::strstr(mRxBuf, str) != nullptr;
    }

    bool waitFor(const char* expect, uint32_t /*timeoutMs*/) {
        loadNextResponse();
        return responseContains(expect);
    }

    void clearRx() { mRxLen = 0; mRxBuf[0] = '\0'; }

    void setIpdPassthrough(bool enable) { mIpdPassthrough = enable; }
    bool isIpdPassthrough() const { return mIpdPassthrough; }

    enum class User : uint8_t { None, Mqtt, Upload };
    void requestAccess(User who) { mCurrentUser = who; }
    void requestAccessAsync(User who) { mRequestedUser = who; }
    void releaseAccess() { mCurrentUser = User::None; }
    bool isAccessRequested() const {
        return mRequestedUser != User::None && mRequestedUser != mCurrentUser;
    }
    void clearRequest() { mRequestedUser = User::None; }

    static const uint16_t RX_BUF_SIZE = 512;

    /* ── Test control surface ────────────────────────────────────────────── */
    void resetForTest() {
        mSentCmds.clear();
        mSentData.clear();
        mPendingResponses.clear();
        mIpdPassthrough = false;
        mCurrentUser    = User::None;
        mRequestedUser  = User::None;
        clearRx();
    }

    /** Queue an AT-command response. Each sendCmd / waitFor / sendData call
     *  pops one response from the queue (if any) into mRxBuf. */
    void pushResponse(const std::string& resp) {
        mPendingResponses.push_back(resp);
    }

    /** Queue raw bytes (e.g. encoded protobuf) as the next response. */
    void pushResponseBytes(const uint8_t* data, uint16_t len) {
        mPendingResponses.emplace_back(reinterpret_cast<const char*>(data), len);
    }

    const std::vector<std::string>&             sentCmds() const { return mSentCmds; }
    const std::vector<std::vector<uint8_t>>&    sentData() const { return mSentData; }

private:
    Esp8266() = default;

    void loadNextResponse() {
        if (mPendingResponses.empty()) return;
        const std::string& s = mPendingResponses.front();
        mRxLen = (uint16_t)(s.size() < RX_BUF_SIZE - 1 ? s.size() : RX_BUF_SIZE - 1);
        std::memcpy(mRxBuf, s.data(), mRxLen);
        mRxBuf[mRxLen] = '\0';
        mPendingResponses.erase(mPendingResponses.begin());
    }

    char     mRxBuf[RX_BUF_SIZE] = {};
    uint16_t mRxLen = 0;
    bool     mIpdPassthrough = false;
    User     mCurrentUser    = User::None;
    User     mRequestedUser  = User::None;

    std::vector<std::string>           mSentCmds;
    std::vector<std::vector<uint8_t>>  mSentData;
    std::vector<std::string>           mPendingResponses;
};

} // namespace arcana
