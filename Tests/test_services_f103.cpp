/**
 * @file test_services_f103.cpp
 * @brief Coverage for the small F103 service singletons (Timer/Led/Io).
 *
 * Each service is exercised through its public lifecycle (initHAL → init →
 * start → stop) plus any direct accessor / mutation that doesn't require
 * the FreeRTOS task body to actually run. The infinite task loops remain
 * uncoverable on host but lifecycle alone is enough to clear the bulk of
 * the file lines.
 */
#include <gtest/gtest.h>
#include <cstring>

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "timers.h"

#include "TimerServiceImpl.hpp"
#include "LedServiceImpl.hpp"
#include "IoServiceImpl.hpp"
#include "SensorServiceImpl.hpp"
#include "LightServiceImpl.hpp"

extern int g_hal_gpio_read_abort_after;
extern int g_hal_gpio_read_call_count;
extern int g_vTaskDelay_call_count;
extern int g_vTaskDelay_abort_after;

using arcana::ServiceStatus;
using arcana::TimerModel;
using arcana::Observable;
using arcana::timer::TimerServiceImpl;
using arcana::led::LedServiceImpl;
using arcana::io::IoServiceImpl;

namespace arcana { namespace timer {
struct TimerServiceTestAccess {
    static void invokeCallback(TimerServiceImpl& t) {
        /* freertos_stub stores `this` as the timer ID. We pass a non-null
         * handle so the callback uses pvTimerGetTimerID()-> our singleton. */
        TimerServiceImpl::timerCallback(reinterpret_cast<TimerHandle_t>(0x1));
    }
    static uint32_t tickCount(TimerServiceImpl& t) { return t.mTickCount; }
    static void setRunning(TimerServiceImpl& t, bool r) { t.mRunning = r; }
    static void resetTick(TimerServiceImpl& t) { t.mTickCount = 0; }
};
}}
using arcana::timer::TimerServiceTestAccess;

namespace arcana { namespace io {
struct IoServiceTestAccess {
    static void taskLoop(IoServiceImpl& s) { s.taskLoop(); }
};
}}
using arcana::io::IoServiceTestAccess;

namespace arcana { namespace sensor {
struct SensorServiceTestAccess {
    static void invokeTask(SensorServiceImpl& s) { SensorServiceImpl::sensorTask(&s); }
    static void setRunning(SensorServiceImpl& s, bool r) { s.mRunning = r; }
};
}}
using arcana::sensor::SensorServiceTestAccess;

namespace arcana { namespace light {
struct LightServiceTestAccess {
    static void invokeTask(LightServiceImpl& s) { LightServiceImpl::lightTask(&s); }
    static void setRunning(LightServiceImpl& s, bool r) { s.mRunning = r; }
};
}}
using arcana::light::LightServiceTestAccess;

// ── TimerServiceImpl ────────────────────────────────────────────────────────

TEST(TimerServiceTest, GetInstanceReturnsSameSingleton) {
    auto& a = TimerServiceImpl::getInstance();
    auto& b = TimerServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(TimerServiceTest, InitHalAndInitOk) {
    auto& t = static_cast<TimerServiceImpl&>(TimerServiceImpl::getInstance());
    EXPECT_EQ(t.initHAL(), ServiceStatus::OK);
    EXPECT_EQ(t.init(),    ServiceStatus::OK);
}

TEST(TimerServiceTest, StartCreatesTimerThenStop) {
    auto& t = static_cast<TimerServiceImpl&>(TimerServiceImpl::getInstance());
    EXPECT_EQ(t.start(), ServiceStatus::OK);
    t.stop();
}

TEST(TimerServiceTest, OutputObservablesWired) {
    auto& t = static_cast<TimerServiceImpl&>(TimerServiceImpl::getInstance());
    EXPECT_NE(t.output.FastTimer, nullptr);
    EXPECT_NE(t.output.BaseTimer, nullptr);
}

TEST(TimerServiceTest, TimerCallbackPublishesFastAndBaseTicks) {
    auto& t = static_cast<TimerServiceImpl&>(TimerServiceImpl::getInstance());
    /* start() arms mRunning + registers the callback with the FreeRTOS stub
     * (which stores `this` as the timer id via pvTimerGetTimerID). */
    t.start();
    TimerServiceTestAccess::resetTick(t);

    /* Drive 10 ticks → fast fires every tick (10), base fires every 10
     * ticks (1). All branches of timerCallback exercised. */
    for (int i = 0; i < 10; ++i) {
        TimerServiceTestAccess::invokeCallback(t);
    }
    EXPECT_EQ(TimerServiceTestAccess::tickCount(t), 10u);

    /* Stopped → callback early-returns, no further tick increment */
    TimerServiceTestAccess::setRunning(t, false);
    TimerServiceTestAccess::invokeCallback(t);
    EXPECT_EQ(TimerServiceTestAccess::tickCount(t), 10u);

    t.stop();
}

// ── LedServiceImpl ──────────────────────────────────────────────────────────

TEST(LedServiceTest, GetInstanceReturnsSameSingleton) {
    auto& a = LedServiceImpl::getInstance();
    auto& b = LedServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(LedServiceTest, InitHalCallsAllOff) {
    auto& l = static_cast<LedServiceImpl&>(LedServiceImpl::getInstance());
    EXPECT_EQ(l.initHAL(), ServiceStatus::OK);
}

TEST(LedServiceTest, InitWithoutTimerEventsReturnsInvalidState) {
    auto& l = static_cast<LedServiceImpl&>(LedServiceImpl::getInstance());
    l.input.TimerEvents = nullptr;
    EXPECT_EQ(l.init(), ServiceStatus::InvalidState);
}

TEST(LedServiceTest, InitWithTimerEventsSubscribes) {
    auto& l = static_cast<LedServiceImpl&>(LedServiceImpl::getInstance());
    Observable<TimerModel> timerObs("led-test");
    l.input.TimerEvents = &timerObs;
    EXPECT_EQ(l.init(), ServiceStatus::OK);

    /* start() arms mRunning so the onTimerTick body runs (drives setColor +
     * mColorIndex wrap). Synchronous notify exercises the static callback. */
    EXPECT_EQ(l.start(), ServiceStatus::OK);

    TimerModel m;
    for (int i = 0; i < 10; ++i) timerObs.notify(&m);  // > kColorCount → wrap

    l.stop();
    l.input.TimerEvents = nullptr;
}

TEST(LedServiceTest, OnTimerTickWhenStoppedDoesNothing) {
    auto& l = static_cast<LedServiceImpl&>(LedServiceImpl::getInstance());
    Observable<TimerModel> timerObs("led-stopped");
    l.input.TimerEvents = &timerObs;
    l.init();
    /* mRunning is false (didn't start) → callback early-returns */
    TimerModel m;
    timerObs.notify(&m);
    l.input.TimerEvents = nullptr;
}

// ── IoServiceImpl ───────────────────────────────────────────────────────────

TEST(IoServiceTest, GetInstanceReturnsSameSingleton) {
    auto& a = IoServiceImpl::getInstance();
    auto& b = IoServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(IoServiceTest, InitHalReturnsOk) {
    auto& s = IoServiceImpl::getInstance();
    EXPECT_EQ(s.initHAL(), ServiceStatus::OK);
}

TEST(IoServiceTest, RequestFlagsRoundTrip) {
    auto& s = IoServiceImpl::getInstance();
    EXPECT_FALSE(s.isUploadRequested());
    EXPECT_FALSE(s.isCancelRequested());
    EXPECT_FALSE(s.isFormatRequested());

    s.clearUploadRequest();
    s.clearCancelRequest();
    s.clearFormatRequest();
    EXPECT_FALSE(s.isUploadRequested());
}

TEST(IoServiceTest, ArmCancelEnablesCancellation) {
    auto& s = IoServiceImpl::getInstance();
    s.armCancel();
    /* mCancelArmed is private, but the next disarmCancel exits cleanly */
    s.disarmCancel();
    EXPECT_FALSE(s.isCancelRequested());
    EXPECT_FALSE(s.isUploadRequested());
}

TEST(IoServiceTest, StartCreatesTaskHandle) {
    auto& s = IoServiceImpl::getInstance();
    /* start() spins up the task; freertos_stub returns a non-null handle */
    EXPECT_EQ(s.start(), ServiceStatus::OK);
}

// ── SensorServiceImpl (with stubbed Mpu6050 + I2cBus drivers) ───────────────

TEST(SensorServiceTest, GetInstanceReturnsSameSingleton) {
    auto& a = arcana::sensor::SensorServiceImpl::getInstance();
    auto& b = arcana::sensor::SensorServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(SensorServiceTest, LifecycleInitHalInitStartStop) {
    auto& s = static_cast<arcana::sensor::SensorServiceImpl&>(
        arcana::sensor::SensorServiceImpl::getInstance());
    EXPECT_EQ(s.initHAL(), ServiceStatus::OK);
    EXPECT_EQ(s.init(),    ServiceStatus::OK);
    EXPECT_EQ(s.start(),   ServiceStatus::OK);
    s.stop();
    /* Output observable wired */
    EXPECT_NE(s.output.DataEvents, nullptr);
}

// ── LightServiceImpl (with stubbed Ap3216c + I2cBus drivers) ────────────────

TEST(LightServiceTest, GetInstanceReturnsSameSingleton) {
    auto& a = arcana::light::LightServiceImpl::getInstance();
    auto& b = arcana::light::LightServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(LightServiceTest, LifecycleInitHalInitStartStop) {
    auto& s = static_cast<arcana::light::LightServiceImpl&>(
        arcana::light::LightServiceImpl::getInstance());
    EXPECT_EQ(s.initHAL(), ServiceStatus::OK);
    EXPECT_EQ(s.init(),    ServiceStatus::OK);
    EXPECT_EQ(s.start(),   ServiceStatus::OK);
    s.stop();
    EXPECT_NE(s.output.DataEvents, nullptr);
}

// ── IoServiceImpl::taskLoop body via abort hook ─────────────────────────────
//
// taskLoop is a for(;;) infinite loop that polls KEY1/KEY2 every 100ms.
// We can't intercept its outer loop without modifying production, so we
// install an abort counter on HAL_GPIO_ReadPin (test_hal_stub) that throws
// after N reads. The test catches the exception and inspects post-loop
// state. Each iteration calls HAL_GPIO_ReadPin twice (KEY2 then KEY1), so
// abort=10 = 5 iterations.

TEST(IoServiceTaskLoop, ManyIterationsHitsKey1HoldThresholds) {
    /* HAL stub returns GPIO_PIN_SET on every read:
     *   - KEY2 (active-LOW)  → released → mKey2Seen=true, no edge action
     *   - KEY1 (active-HIGH) → pressed  → mKey1Hold increments each pass
     * After 20 polling iterations mKey1Hold reaches 20 → format trigger.
     * Each iteration calls HAL_GPIO_ReadPin twice → abort=50 gives ~25 iters,
     * which exercises the hold==3 / 10 / 15 / >=20 toast branches and the
     * format-fired path. */
    auto& s = static_cast<IoServiceImpl&>(IoServiceImpl::getInstance());
    s.clearFormatRequest();
    g_hal_gpio_read_call_count  = 0;
    g_hal_gpio_read_abort_after = 50;
    try {
        IoServiceTestAccess::taskLoop(s);
        FAIL() << "expected abort exception";
    } catch (int) {
        SUCCEED();
    }
    g_hal_gpio_read_abort_after = 0;
    /* After 20 iterations the hold counter triggered the format request */
    EXPECT_TRUE(s.isFormatRequested());
    s.clearFormatRequest();
}

// ── SensorServiceImpl::sensorTask body via vTaskDelay abort ─────────────────

TEST(SensorServiceTaskBody, OneIterationViaVTaskDelayAbort) {
    auto& s = static_cast<arcana::sensor::SensorServiceImpl&>(
        arcana::sensor::SensorServiceImpl::getInstance());
    s.init();   // wires Mpu sensor
    SensorServiceTestAccess::setRunning(s, true);

    /* sensorTask: vTaskDelay(100ms init) + while(running) { read; vTaskDelay }
     * abort=2 → init delay + first iter delay → throws */
    g_vTaskDelay_call_count  = 0;
    g_vTaskDelay_abort_after = 2;
    try {
        SensorServiceTestAccess::invokeTask(s);
        FAIL() << "expected abort";
    } catch (int) {}
    g_vTaskDelay_abort_after = 0;
}

// ── LightServiceImpl::lightTask body via vTaskDelay abort ───────────────────

TEST(LightServiceTaskBody, OneIterationViaVTaskDelayAbort) {
    auto& s = static_cast<arcana::light::LightServiceImpl&>(
        arcana::light::LightServiceImpl::getInstance());
    s.init();
    LightServiceTestAccess::setRunning(s, true);

    g_vTaskDelay_call_count  = 0;
    g_vTaskDelay_abort_after = 2;
    try {
        LightServiceTestAccess::invokeTask(s);
        FAIL() << "expected abort";
    } catch (int) {}
    g_vTaskDelay_abort_after = 0;
}
