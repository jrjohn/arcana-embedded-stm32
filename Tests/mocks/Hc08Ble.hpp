/**
 * @file Hc08Ble.hpp (host test stub)
 *
 * Programmable mock for the HC-08 BLE driver. Tests inspect what the
 * code-under-test sent via `sentBytes()` and feed reassembled frames back via
 * `pushFrame(...)`. Surface matches the real Hc08Ble.hpp closely enough that
 * BleServiceImpl compiles and runs on host without modification.
 */
#pragma once

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace arcana {

/**
 * Lightweight Hc08Ble surrogate. The real driver assembles bytes via
 * FrameAssembler and exposes the completed frame; here we let tests stage a
 * sequence of fake "frames" via pushFrame() and have processRxRing() pop
 * them one at a time. waitForData() simply returns the size of the next
 * staged frame (or 0).
 */
class Hc08Ble {
public:
    static Hc08Ble& getInstance();

    bool initHAL() { return true; }

    bool sendCmd(const char* /*cmd*/, const char* /*expect*/ = "OK",
                 uint32_t /*timeoutMs*/ = 500) { return true; }

    bool send(const uint8_t* data, uint16_t len, uint32_t /*timeoutMs*/ = 200) {
        mSent.emplace_back(data, data + len);
        return mSendOk;
    }

    const char* getResponse() const { return ""; }
    uint16_t getResponseLen() const { return 0; }
    void clearRx() {}

    /** waitForData reports how many bytes the next staged frame holds. */
    uint16_t waitForData(uint32_t /*timeoutMs*/) {
        ++mPollCount;
        if (mStopFlag && mPollCount >= mStopAfterPolls) *mStopFlag = false;
        if (mFrames.empty()) return 0;
        return static_cast<uint16_t>(mFrames.front().size());
    }

    /** Return true if a staged frame is ready (and load it into mCurrent). */
    bool processRxRing() {
        if (mFrames.empty()) return false;
        mCurrent = mFrames.front();
        mFrames.erase(mFrames.begin());
        return true;
    }

    const uint8_t* getFrame() const { return mCurrent.data(); }
    uint16_t getFrameLen() const { return static_cast<uint16_t>(mCurrent.size()); }
    void resetFrame() { mCurrent.clear(); }

    void isr_onRxByte(uint8_t /*byte*/) {}
    void isr_onIdle() {}

    void setDataMode(bool dataMode) { mDataMode = dataMode; }
    bool isDataMode() const { return mDataMode; }

    /* ── Test control surface ────────────────────────────────────────────── */

    /** Drop all internal state — call before each test. */
    void resetForTest() {
        mSent.clear();
        mFrames.clear();
        mCurrent.clear();
        mDataMode = false;
        mSendOk = true;
    }

    /** Stage a fake assembled frame for the next processRxRing() pop. */
    void pushFrame(const uint8_t* data, uint16_t len) {
        mFrames.emplace_back(data, data + len);
    }

    /** Force send() to fail (covers TX-error branches). */
    void setSendOk(bool ok) { mSendOk = ok; }

    /** After `n` waitForData() polls, write `false` into `*flag`. Used by
     *  tests to break out of taskLoop's outer while after a known iteration
     *  count without instrumenting the production code. */
    void setStopAfterPolls(int n, bool* flag) {
        mPollCount = 0;
        mStopAfterPolls = n;
        mStopFlag = flag;
    }

    const std::vector<std::vector<uint8_t>>& sentBytes() const { return mSent; }

private:
    Hc08Ble() = default;

    std::vector<std::vector<uint8_t>> mSent;
    std::vector<std::vector<uint8_t>> mFrames;
    std::vector<uint8_t>              mCurrent;
    bool                              mDataMode = false;
    bool                              mSendOk   = true;
    int                               mPollCount = 0;
    int                               mStopAfterPolls = 0;
    bool*                             mStopFlag = nullptr;
};

} // namespace arcana
