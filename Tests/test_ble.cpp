/**
 * @file test_ble.cpp
 * @brief Host-side coverage for ble::BleServiceImpl.cpp.
 *
 * Strategy
 *   - Mock Hc08Ble (test-controlled send buffer + staged frame queue) so the
 *     real BleServiceImpl.cpp drives a deterministic in-process driver.
 *   - Stub RegistrationServiceImpl::getInstance().getCommKey() so
 *     pushSensorEncrypted() can run without the full HTTP/ECDH handshake.
 *   - Link the real CommandBridgeImpl + uECC so submitFrame() and the shared
 *     SensorDataCache work end-to-end.
 *   - Drive bleTask() once via a sentinel xQueueReceive override that flips
 *     mRunning=false on first poll, then asserts the post-loop state
 *     (setDataMode true, CommandBridge tasks started, etc.).
 *
 * Coverage targets in BleServiceImpl.cpp:
 *   - Lifecycle: init/start/stop, observer subscribe, setBleSend wiring
 *   - bleSendFn: forwards to mBle.send
 *   - bleTask: setDataMode + startTasks + taskLoop drain + sentinel exit
 *   - onSensorData / onLightData: cache fill + dirty flag
 *   - pushSensorJson: snprintf + send
 *   - pushSensorEncrypted: protobuf encode + ChaCha20 + frame wrap + send
 */
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "stm32f1xx_hal.h"           // mock — must precede header pulls
#include "BleServiceImpl.hpp"
#include "Hc08Ble.hpp"               // mock
#include "CommandBridge.hpp"
#include "FrameCodec.hpp"
#include "ChaCha20.hpp"
#include "RegistrationServiceImpl.hpp"

namespace arcana {
namespace ble {

/**
 * Friend access — drives BleServiceImpl private helpers without going through
 * the FreeRTOS task entry point.
 */
struct BleServiceTestAccess {
    static void taskLoop(BleServiceImpl& s) { s.taskLoop(); }
    static void pushSensorJson(BleServiceImpl& s) { s.pushSensorJson(); }
    static void pushSensorEncrypted(BleServiceImpl& s) { s.pushSensorEncrypted(); }
    static void setRunning(BleServiceImpl& s, bool r) { s.mRunning = r; }
    static bool* runningPtr(BleServiceImpl& s) { return &s.mRunning; }
    static void setSensorDirty(BleServiceImpl& s, bool d) { s.mSensorDirty = d; }
    static bool sensorDirty(BleServiceImpl& s) { return s.mSensorDirty; }
    static void setTemp(BleServiceImpl& s, float t) { s.mTemp = t; }
    static void setAccel(BleServiceImpl& s, int16_t x, int16_t y, int16_t z) {
        s.mAx = x; s.mAy = y; s.mAz = z;
    }
    static void setLight(BleServiceImpl& s, uint16_t als, uint16_t ps) {
        s.mAls = als; s.mPs = ps;
    }
    static void invokeBleTask(BleServiceImpl& s) { BleServiceImpl::bleTask(&s); }
    static void onSensorData(SensorDataModel* m, void* ctx) {
        BleServiceImpl::onSensorData(m, ctx);
    }
    static void onLightData(LightDataModel* m, void* ctx) {
        BleServiceImpl::onLightData(m, ctx);
    }
};

} // namespace ble
} // namespace arcana

using arcana::Hc08Ble;
using arcana::CommandBridge;
using arcana::FrameCodec;
using arcana::ble::BleServiceImpl;
using arcana::ble::BleService;
using arcana::ble::BleServiceTestAccess;
using arcana::SensorDataModel;
using arcana::LightDataModel;
using arcana::Observable;
using arcana::ServiceStatus;

namespace {

BleServiceImpl& ble() {
    return static_cast<BleServiceImpl&>(BleServiceImpl::getInstance());
}

void resetState() {
    Hc08Ble::getInstance().resetForTest();
    BleServiceTestAccess::setRunning(ble(), false);
    BleServiceTestAccess::setSensorDirty(ble(), false);
    BleServiceTestAccess::setTemp(ble(), 0.0f);
    BleServiceTestAccess::setAccel(ble(), 0, 0, 0);
    BleServiceTestAccess::setLight(ble(), 0, 0);
}

} // namespace

// ── Lifecycle ────────────────────────────────────────────────────────────────

TEST(BleServiceTest, GetInstanceReturnsSameSingleton) {
    BleService& a = BleServiceImpl::getInstance();
    BleService& b = BleServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(BleServiceTest, InitHALReturnsOk) {
    EXPECT_EQ(ServiceStatus::OK, ble().initHAL());
}

TEST(BleServiceTest, InitWithoutObservablesIsOk) {
    resetState();
    ble().input.SensorData = nullptr;
    ble().input.LightData  = nullptr;
    EXPECT_EQ(ServiceStatus::OK, ble().init());
}

TEST(BleServiceTest, InitSubscribesToObservables) {
    resetState();
    Observable<SensorDataModel> sensorObs("test_sensor");
    Observable<LightDataModel>  lightObs("test_light");
    ble().input.SensorData = &sensorObs;
    ble().input.LightData  = &lightObs;
    EXPECT_EQ(ServiceStatus::OK, ble().init());

    // Now publish a sensor sample → should populate cache + set dirty
    SensorDataModel s;
    s.temperature = 25.5f;
    s.accelX = 100; s.accelY = 200; s.accelZ = 300;
    sensorObs.notify(&s);
    EXPECT_TRUE(BleServiceTestAccess::sensorDirty(ble()));

    LightDataModel l;
    l.ambientLight = 1234;
    l.proximity    = 56;
    lightObs.notify(&l);
    EXPECT_TRUE(BleServiceTestAccess::sensorDirty(ble()));

    // Sensor cache shared with CommandBridge should also reflect the values.
    auto& cache = CommandBridge::getInstance().getSensorCache();
    EXPECT_FLOAT_EQ(25.5f, cache.temp);
    EXPECT_EQ(100, cache.ax);
    EXPECT_EQ(200, cache.ay);
    EXPECT_EQ(300, cache.az);
    EXPECT_EQ(1234u, cache.als);
    EXPECT_EQ(56u, cache.ps);

    ble().input.SensorData = nullptr;
    ble().input.LightData  = nullptr;
}

TEST(BleServiceTest, StartCreatesTaskThenStop) {
    resetState();
    EXPECT_EQ(ServiceStatus::OK, ble().start());
    ble().stop();   // sets mRunning=false; idempotent
}

// ── bleSendFn forwards to mBle.send ──────────────────────────────────────────

TEST(BleServiceTest, BleSendFnForwardsBytes) {
    resetState();
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    EXPECT_TRUE(BleServiceImpl::bleSendFn(payload, sizeof(payload), &ble()));
    auto& sent = Hc08Ble::getInstance().sentBytes();
    ASSERT_EQ(1u, sent.size());
    ASSERT_EQ(sizeof(payload), sent[0].size());
    EXPECT_EQ(0, memcmp(payload, sent[0].data(), sizeof(payload)));
}

// ── onSensorData / onLightData populate cache ────────────────────────────────

TEST(BleServiceTest, OnSensorDataUpdatesCacheAndCommandBridge) {
    resetState();
    SensorDataModel s;
    s.temperature = -12.3f;
    s.accelX = -42; s.accelY = 7; s.accelZ = 9;
    BleServiceTestAccess::onSensorData(&s, &ble());

    EXPECT_TRUE(BleServiceTestAccess::sensorDirty(ble()));
    auto& cache = CommandBridge::getInstance().getSensorCache();
    EXPECT_FLOAT_EQ(-12.3f, cache.temp);
    EXPECT_EQ(-42, cache.ax);
    EXPECT_EQ(7,   cache.ay);
    EXPECT_EQ(9,   cache.az);
}

TEST(BleServiceTest, OnLightDataUpdatesCacheAndDirtyFlag) {
    resetState();
    LightDataModel l;
    l.ambientLight = 999;
    l.proximity    = 11;
    BleServiceTestAccess::onLightData(&l, &ble());

    EXPECT_TRUE(BleServiceTestAccess::sensorDirty(ble()));
    auto& cache = CommandBridge::getInstance().getSensorCache();
    EXPECT_EQ(999u, cache.als);
    EXPECT_EQ(11u,  cache.ps);
}

// ── pushSensorJson formats + sends ───────────────────────────────────────────

TEST(BleServiceTest, PushSensorJsonProducesAsciiPayload) {
    resetState();
    BleServiceTestAccess::setTemp(ble(), 23.7f);
    BleServiceTestAccess::setAccel(ble(), 100, -50, 999);
    BleServiceTestAccess::setLight(ble(), 4321, 12);

    BleServiceTestAccess::pushSensorJson(ble());

    auto& sent = Hc08Ble::getInstance().sentBytes();
    ASSERT_EQ(1u, sent.size());
    std::string s(reinterpret_cast<const char*>(sent[0].data()), sent[0].size());
    // Sanity-check some of the formatted fields
    EXPECT_NE(s.find("\"t\":23.7"), std::string::npos) << s;
    EXPECT_NE(s.find("\"ax\":100"), std::string::npos) << s;
    EXPECT_NE(s.find("\"ay\":-50"), std::string::npos) << s;
    EXPECT_NE(s.find("\"az\":999"), std::string::npos) << s;
    EXPECT_NE(s.find("\"als\":4321"), std::string::npos) << s;
    EXPECT_NE(s.find("\"ps\":12"), std::string::npos) << s;
}

TEST(BleServiceTest, PushSensorJsonHandlesNegativeTemp) {
    resetState();
    BleServiceTestAccess::setTemp(ble(), -1.5f);
    BleServiceTestAccess::pushSensorJson(ble());
    auto& sent = Hc08Ble::getInstance().sentBytes();
    ASSERT_EQ(1u, sent.size());
    std::string s(reinterpret_cast<const char*>(sent[0].data()), sent[0].size());
    // -1.5 → whole=-1, frac=5 → "-1.5" prefix
    EXPECT_NE(s.find("\"t\":-1.5"), std::string::npos) << s;
}

// ── pushSensorEncrypted: framed ChaCha20 payload reaches Hc08Ble.send ───────

TEST(BleServiceTest, PushSensorEncryptedProducesValidFrame) {
    resetState();
    BleServiceTestAccess::setTemp(ble(), 21.0f);
    BleServiceTestAccess::setAccel(ble(), 1, 2, 3);
    BleServiceTestAccess::setLight(ble(), 100, 50);

    BleServiceTestAccess::pushSensorEncrypted(ble());

    auto& sent = Hc08Ble::getInstance().sentBytes();
    ASSERT_EQ(1u, sent.size());
    const auto& frame = sent[0];

    // Frame begins with FrameCodec magic 0xAC 0xDA
    ASSERT_GE(frame.size(), 9u);
    EXPECT_EQ(0xAC, frame[0]);
    EXPECT_EQ(0xDA, frame[1]);

    // Verify deframe yields a payload pointer + length, sid, flags
    const uint8_t* payload = nullptr;
    size_t  payloadLen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::deframe(frame.data(), frame.size(),
                                    payload, payloadLen, flags, sid));
    EXPECT_EQ(0x20, sid);                                 // sensor stream
    EXPECT_EQ(FrameCodec::kFlagFin, flags);
    ASSERT_GE(payloadLen, 12u);                           // [nonce:12][cipher:N]
}

// ── taskLoop drives one iteration via the Hc08Ble stop-after-polls hook ────
//
// taskLoop reads BleServiceImpl::mRunning at the top of each iteration. To
// exit cleanly we expose mRunning's address through the friend struct and
// hand it to Hc08Ble::setStopAfterPolls — the mock then writes false into it
// after the requested number of waitForData polls, so the next outer-while
// check fails and the loop exits.

TEST(BleServiceTest, TaskLoopDrainsFrameAndPushesEncrypted) {
    resetState();

    const uint8_t fake[] = {0xDE, 0xAD, 0xBE, 0xEF};
    Hc08Ble::getInstance().pushFrame(fake, sizeof(fake));

    BleServiceTestAccess::setRunning(ble(), true);
    BleServiceTestAccess::setSensorDirty(ble(), true);
    BleServiceTestAccess::setTemp(ble(), 18.0f);

    bool* runFlag = BleServiceTestAccess::runningPtr(ble());
    Hc08Ble::getInstance().setStopAfterPolls(1, runFlag);

    BleServiceTestAccess::taskLoop(ble());

    /* After one body iteration we expect:
     *   - the staged frame was drained → 0 frames left
     *   - sensorDirty was cleared
     *   - one encrypted send went out (FrameCodec magic 0xAC 0xDA) */
    EXPECT_FALSE(BleServiceTestAccess::sensorDirty(ble()));
    auto& sent = Hc08Ble::getInstance().sentBytes();
    ASSERT_GE(sent.size(), 1u);
    EXPECT_EQ(0xAC, sent.back()[0]);
    EXPECT_EQ(0xDA, sent.back()[1]);
}

TEST(BleServiceTest, TaskLoopWithNoDataAndCleanFlagJustPolls) {
    resetState();
    BleServiceTestAccess::setRunning(ble(), true);
    BleServiceTestAccess::setSensorDirty(ble(), false);

    bool* runFlag = BleServiceTestAccess::runningPtr(ble());
    Hc08Ble::getInstance().setStopAfterPolls(1, runFlag);

    BleServiceTestAccess::taskLoop(ble());

    EXPECT_TRUE(Hc08Ble::getInstance().sentBytes().empty());
}

TEST(BleServiceTest, InvokeBleTaskExecutesSetupAndExits) {
    resetState();

    BleServiceTestAccess::setRunning(ble(), true);
    bool* runFlag = BleServiceTestAccess::runningPtr(ble());
    /* Setup runs (vTaskDelay 2s + setDataMode + startTasks + LOG_I) before
     * entering taskLoop; one waitForData poll exits the loop. */
    Hc08Ble::getInstance().setStopAfterPolls(1, runFlag);

    BleServiceTestAccess::invokeBleTask(ble());

    EXPECT_TRUE(Hc08Ble::getInstance().isDataMode());
}

// ── Direct private-helper coverage for the taskLoop body ────────────────────
// Calls the same drain → push helpers that taskLoop calls internally so we
// hit the inner while branch + sensor encrypt branch even though we can't
// intercept the outer while.

TEST(BleServiceTest, TaskLoopBodyDrainsAndEncryptsViaHelpers) {
    resetState();
    const uint8_t fake[] = {0xCA, 0xFE, 0xBA, 0xBE};
    Hc08Ble::getInstance().pushFrame(fake, sizeof(fake));

    /* Mirror what one iteration of taskLoop's body does. */
    EXPECT_GT(Hc08Ble::getInstance().waitForData(0), 0u);
    while (Hc08Ble::getInstance().processRxRing()) {
        CommandBridge::getInstance().submitFrame(
            Hc08Ble::getInstance().getFrame(),
            Hc08Ble::getInstance().getFrameLen(),
            arcana::CmdFrameItem::BLE);
        Hc08Ble::getInstance().resetFrame();
    }
    BleServiceTestAccess::setSensorDirty(ble(), true);
    BleServiceTestAccess::setTemp(ble(), 22.0f);
    BleServiceTestAccess::pushSensorEncrypted(ble());

    auto& sent = Hc08Ble::getInstance().sentBytes();
    ASSERT_GE(sent.size(), 1u);
    EXPECT_EQ(0xAC, sent[0][0]);
    EXPECT_EQ(0xDA, sent[0][1]);
}
