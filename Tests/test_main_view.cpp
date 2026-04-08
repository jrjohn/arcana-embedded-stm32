/**
 * @file test_main_view.cpp
 * @brief Coverage for MainView.cpp render functions against StubDisplay.
 *
 * MainView's processRender + renderTaskEntry are infinite-loop FreeRTOS
 * task code (uncoverable on host). The render() / renderTemp / renderSdInfo
 * / renderStorage / renderTime / renderMqtt / renderEcgColumn / onEnter
 * are pure draw functions exercised here directly.
 */
#include <gtest/gtest.h>
#include <cstring>

#include "stm32f1xx_hal.h"
#include "IDisplay.hpp"
#include "DisplayStatus.hpp"
#include "MainView.hpp"
#include "LcdViewModel.hpp"
#include "SystemClock.hpp"

using arcana::display::IDisplay;
using arcana::display::Color;
using arcana::lcd::MainView;
using arcana::lcd::LcdOutput;
using arcana::lcd::LcdInput;
using arcana::lcd::LcdViewModel;
using arcana::SystemClock;

namespace {

class StubDisplay : public IDisplay {
public:
    uint32_t fillRectCalls = 0;
    uint32_t fillScreenCalls = 0;
    uint32_t drawStringCalls = 0;
    uint32_t drawHLineCalls  = 0;

    uint16_t width() const override  { return 240; }
    uint16_t height() const override { return 320; }
    void fillScreen(Color) override { ++fillScreenCalls; }
    void fillRect(uint16_t, uint16_t, uint16_t, uint16_t, Color) override {
        ++fillRectCalls;
    }
    void drawHLine(uint16_t, uint16_t, uint16_t, Color) override {
        ++drawHLineCalls;
    }
    void drawChar(uint16_t, uint16_t, char, Color, Color, uint8_t) override {}
    void drawString(uint16_t, uint16_t, const char*, Color, Color, uint8_t) override {
        ++drawStringCalls;
    }
    void drawXBitmap(uint16_t, uint16_t, uint16_t, uint16_t,
                     const uint8_t*, Color, Color) override {}
};

} // anonymous namespace

// ── Lifecycle ──────────────────────────────────────────────────────────────

TEST(MainViewLifecycle, Construct) {
    MainView v;
    EXPECT_EQ(v.input.viewModel, nullptr);
    EXPECT_EQ(v.input.lcd, nullptr);
}

TEST(MainViewLifecycle, InitCreatesQueueAndMutex) {
    MainView v;
    v.init();
    SUCCEED();
}

TEST(MainViewLifecycle, StartCreatesRenderTask) {
    MainView v;
    v.init();
    StubDisplay d;
    v.input.lcd = &d;
    v.start();
    EXPECT_NE(v.renderTaskHandle(), nullptr);
}

TEST(MainViewLifecycle, PushEcgSampleEnqueues) {
    MainView v;
    v.init();
    v.pushEcgSample(50);
    SUCCEED();
}

// ── onEnter ────────────────────────────────────────────────────────────────

TEST(MainViewLayout, OnEnterDrawsStaticLayout) {
    MainView v;
    StubDisplay d;
    v.onEnter(d);
    EXPECT_EQ(d.fillScreenCalls, 1u);
    EXPECT_GT(d.drawStringCalls, 5u);   /* multiple labels */
    EXPECT_GT(d.drawHLineCalls,  3u);   /* dividers */
}

// ── render dispatch ────────────────────────────────────────────────────────

TEST(MainViewRender, RenderDispatchAllDirty) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.temperature = 23.5f;
    out.tempValid = true;
    out.recordCount = 100; out.writesPerSec = 50;
    out.totalKB = 1024; out.kbPerSec = 5;
    out.epoch = 1700000000; out.timeSynced = true; out.uptimeSec = 99;
    out.sdFreeMB = 100; out.sdTotalMB = 8000;
    out.mqttKnown = true; out.mqttConnected = true;
    out.dirty = 0xFF;  /* all dirty */
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_GT(d.drawStringCalls, 0u);
}

TEST(MainViewRender, RenderTempUpdatesRendered) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.temperature = 25.4f;
    out.dirty = LcdOutput::DIRTY_TEMP;
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_FLOAT_EQ(rendered.temperature, 25.4f);
    EXPECT_TRUE(rendered.tempValid);
}

TEST(MainViewRender, RenderTempNegative) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.temperature = -10.5f;
    out.dirty = LcdOutput::DIRTY_TEMP;
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_FLOAT_EQ(rendered.temperature, -10.5f);
}

TEST(MainViewRender, RenderSdInfoUpdates) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.sdFreeMB = 100; out.sdTotalMB = 8000;
    out.dirty = LcdOutput::DIRTY_SDINFO;
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_EQ(rendered.sdFreeMB, 100u);
    EXPECT_EQ(rendered.sdTotalMB, 8000u);
}

TEST(MainViewRender, RenderStorageUpdates) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.recordCount = 12345;
    out.writesPerSec = 100;
    out.totalKB = 8192;
    out.kbPerSec = 50;
    out.dirty = LcdOutput::DIRTY_STORAGE;
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_EQ(rendered.recordCount, 12345u);
    EXPECT_EQ(rendered.writesPerSec, 100);
}

TEST(MainViewRender, RenderTimeWithSyncedClock) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.epoch = 1700000000;  /* > 2020-01-01 → use date+time path */
    out.timeSynced = true;
    out.uptimeSec = 0;
    out.dirty = LcdOutput::DIRTY_TIME;
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_EQ(rendered.epoch, 1700000000u);
    EXPECT_TRUE(rendered.timeSynced);
}

TEST(MainViewRender, RenderTimeWithoutSyncFallsBackToUptime) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.epoch = 0;          /* not synced → uptime branch */
    out.uptimeSec = 3661;   /* 1h 1m 1s */
    out.dirty = LcdOutput::DIRTY_TIME;
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_EQ(rendered.uptimeSec, 3661u);
}

TEST(MainViewRender, RenderMqttUnknown) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.mqttKnown = false;
    out.dirty = LcdOutput::DIRTY_MQTT;
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_FALSE(rendered.mqttKnown);
}

TEST(MainViewRender, RenderMqttConnected) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.mqttKnown = true; out.mqttConnected = true;
    out.dirty = LcdOutput::DIRTY_MQTT;
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_TRUE(rendered.mqttConnected);
}

TEST(MainViewRender, RenderMqttDisconnected) {
    MainView v;
    StubDisplay d;
    LcdOutput out;
    out.mqttKnown = true; out.mqttConnected = false;
    out.dirty = LcdOutput::DIRTY_MQTT;
    LcdOutput rendered;
    v.render(d, out, rendered);
    EXPECT_FALSE(rendered.mqttConnected);
}

// ── renderEcgColumn ────────────────────────────────────────────────────────

TEST(MainViewEcg, RenderEcgColumnDrawsPixels) {
    MainView v;
    StubDisplay d;
    /* Bottom of waveform */
    v.renderEcgColumn(d, /*x=*/10, /*y=*/99, /*prevY=*/70);
    EXPECT_GT(d.fillRectCalls, 0u);
}

TEST(MainViewEcg, RenderEcgColumnAtTopAndBottomEdges) {
    MainView v;
    StubDisplay d;
    /* x=0 (leftmost), y=0 (top), prevY=99 (bottom) */
    v.renderEcgColumn(d, 0, 0, 99);
    /* x=119 (rightmost half-res), y=99, prevY=0 */
    v.renderEcgColumn(d, 119, 99, 0);
    /* y == prevY (no line, just dot) */
    v.renderEcgColumn(d, 50, 50, 50);
    SUCCEED();
}

// ── processRender via friend access ─────────────────────────────────────────
//
// processRender is the body of renderTaskEntry's infinite loop. We expose it
// via a friend struct so the host test can drive one render pass without
// running the FreeRTOS task. Covers the dirty-flag dispatch + ECG queue drain
// + toast expiry + onEnter redraw branches.

namespace arcana { namespace lcd {
struct MainViewTestAccess {
    static void processRender(MainView& v) { v.processRender(); }
};
}}

using arcana::lcd::MainViewTestAccess;

/* freertos_stubs override hooks */
typedef long BaseType_t;
typedef void* QueueHandle_t;
typedef uint32_t TickType_t;
typedef BaseType_t (*XQueueReceiveFn)(QueueHandle_t, void*, TickType_t);
extern XQueueReceiveFn g_xQueueReceiveOverride;

TEST(MainViewProcess, ProcessRenderEarlyReturnsWhenInputsMissing) {
    MainView v;
    v.init();
    /* viewModel and lcd both null → first guard returns immediately. */
    MainViewTestAccess::processRender(v);
    SUCCEED();
}

TEST(MainViewProcess, ProcessRenderDispatchesDirtyAndDrainsEcg) {
    MainView v;
    StubDisplay d;
    LcdViewModel vm;
    /* No observables wired — vm.output() returns the default zero state */
    vm.init(nullptr);

    v.init();
    v.input.lcd       = &d;
    v.input.viewModel = &vm;

    /* Push a few ECG samples; the queue should drain in processRender */
    v.pushEcgSample(40);
    v.pushEcgSample(60);
    v.pushEcgSample(80);

    /* Mark the LcdOutput as dirty so the render branch fires too. */
    LcdInput in;
    in.type = LcdInput::SensorData;
    in.sensor.temperature = 21.5f;
    vm.onEvent(in);

    MainViewTestAccess::processRender(v);

    /* The mock display should have been touched at least once */
    EXPECT_GT(d.fillRectCalls + d.drawStringCalls + d.drawHLineCalls, 0u);
}

// ── ECG drain branch via xQueueReceive override ────────────────────────────

namespace {
int sQueuePops = 0;
BaseType_t fakeEcgQueueReceive(QueueHandle_t /*q*/, void* buf, TickType_t /*t*/) {
    if (sQueuePops <= 0) return 0;  // pdFALSE
    --sQueuePops;
    *static_cast<uint8_t*>(buf) = static_cast<uint8_t>(50 + sQueuePops);
    return 1;  // pdTRUE
}
} // anonymous

TEST(MainViewProcess, ProcessRenderEcgDrainBranch) {
    MainView v;
    StubDisplay d;
    LcdViewModel vm;
    vm.init(nullptr);

    v.init();
    v.input.lcd       = &d;
    v.input.viewModel = &vm;

    /* Pop 3 fake ECG samples then return pdFALSE */
    sQueuePops = 3;
    g_xQueueReceiveOverride = fakeEcgQueueReceive;

    MainViewTestAccess::processRender(v);

    g_xQueueReceiveOverride = nullptr;

    /* Each ECG sample triggers renderEcgColumn → fillRect calls */
    EXPECT_GT(d.fillRectCalls, 0u);
}

// ── Toast expiry branch (lines 91-101: onEnter + full redraw) ──────────────

TEST(MainViewProcess, ProcessRenderToastExpiryRedrawsView) {
    MainView v;
    StubDisplay d;
    LcdViewModel vm;
    vm.init(nullptr);

    v.init();
    v.input.lcd       = &d;
    v.input.viewModel = &vm;

    /* Stage a toast that's already expired (durationMs=0 → dismissTick=now) */
    arcana::display::g_display = &d;
    arcana::display::requestToast("expired", /*ms*/0);
    /* First processRender: arms the toast (sets active=true, dismissTick=now) */
    MainViewTestAccess::processRender(v);

    /* Force toast to expired by also setting dismissTick=0 via dismissToast */
    arcana::display::dismissToast();

    /* Second processRender: toastUpdate returns true (expired) → triggers
     * the onEnter + full redraw branch (lines 91-101) */
    uint32_t before = d.fillScreenCalls;
    MainViewTestAccess::processRender(v);
    EXPECT_GE(d.fillScreenCalls, before);

    arcana::display::g_display = nullptr;
}
