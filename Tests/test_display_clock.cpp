/**
 * @file test_display_clock.cpp
 * @brief Header-only coverage for the production SystemClock + DisplayStatus
 *        toast helpers + BitmapButton widget.
 *
 * For SystemClock we deliberately bypass the mocks/SystemClock.hpp stub by
 * including the production header from F103_COMMON via the test target's
 * include order (F103_COMMON before MOCKS_DIR). The RTC register stubs in
 * stm32f1xx_hal.h cover CNTH/CNTL/CRL access; rtcEnterConfig/rtcExitConfig
 * spin on RTC_CRL_RTOFF which the host stub initialises to "ready".
 */
#include <gtest/gtest.h>
#include <cstring>

#include "stm32f1xx_hal.h"

/* Production SystemClock — F103_COMMON precedes MOCKS_DIR in this target */
#include "SystemClock.hpp"

#include "IDisplay.hpp"
#include "DisplayStatus.hpp"
#include "BitmapButton.hpp"
#include "TouchTypes.hpp"

using arcana::SystemClock;
using arcana::display::IDisplay;
using arcana::display::Color;
namespace dsp = arcana::display;

namespace {

class StubDisplay : public IDisplay {
public:
    uint32_t fillRectCalls = 0;
    uint32_t fillScreenCalls = 0;
    uint32_t drawStringCalls = 0;
    uint32_t drawHLineCalls = 0;
    uint32_t drawXBitmapCalls = 0;
    uint16_t lastBitmapW = 0, lastBitmapH = 0;

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
    void drawXBitmap(uint16_t, uint16_t, uint16_t w, uint16_t h,
                     const uint8_t*, Color, Color) override {
        ++drawXBitmapCalls;
        lastBitmapW = w;
        lastBitmapH = h;
    }
};

void resetGlobalDisplay(IDisplay* d) { dsp::g_display = d; }

void resetToast() {
    /* Drain any leftover state from previous tests by dismissing + processing */
    dsp::dismissToast();
    StubDisplay tmp; resetGlobalDisplay(&tmp);
    dsp::toastUpdate(0);
    /* Drain any pending request by reading + clearing */
    auto& req = dsp::toastRequest();
    req.pending = false;
    req.dismiss = false;
    auto& st = dsp::toastState();
    st.active = false;
    resetGlobalDisplay(nullptr);
}

} // anonymous namespace

// ── SystemClock (production header, RTC register-backed) ────────────────────

TEST(SystemClockProd, SyncWritesEpochToRtcCounter) {
    /* Reset RTC counter to 0 */
    RTC->CNTH = 0;
    RTC->CNTL = 0;

    SystemClock::getInstance().sync(1700000000u);
    EXPECT_EQ(SystemClock::getInstance().now(), 1700000000u);
    EXPECT_TRUE(SystemClock::getInstance().isSynced());
}

TEST(SystemClockProd, IsSyncedReadsRtcAboveYear2020) {
    /* Manually write a year-2024 epoch into RTC to simulate VBAT-persisted time */
    uint32_t epoch = 1717000000u;  // 2024-05-29
    RTC->CNTH = (uint16_t)(epoch >> 16);
    RTC->CNTL = (uint16_t)(epoch & 0xFFFF);
    EXPECT_TRUE(SystemClock::getInstance().isSynced());
}

TEST(SystemClockProd, NowReadsRtcCounter) {
    uint32_t epoch = 1730000000u;
    RTC->CNTH = (uint16_t)(epoch >> 16);
    RTC->CNTL = (uint16_t)(epoch & 0xFFFF);
    EXPECT_EQ(SystemClock::getInstance().now(), epoch);
}

TEST(SystemClockProd, LocalNowAddsTimezoneOffset) {
    SystemClock::getInstance().setTzOffset(480);  // UTC+8
    EXPECT_EQ(SystemClock::getInstance().tzOffsetMin(), 480);

    uint32_t epoch = 1700000000u;
    RTC->CNTH = (uint16_t)(epoch >> 16);
    RTC->CNTL = (uint16_t)(epoch & 0xFFFF);
    EXPECT_EQ(SystemClock::getInstance().localNow(), epoch + 480 * 60);
}

TEST(SystemClockProd, NegativeTimezoneOffset) {
    SystemClock::getInstance().setTzOffset(-300);  // UTC-5
    uint32_t epoch = 1700000000u;
    RTC->CNTH = (uint16_t)(epoch >> 16);
    RTC->CNTL = (uint16_t)(epoch & 0xFFFF);
    EXPECT_EQ(SystemClock::getInstance().localNow(), epoch - 300 * 60);
    SystemClock::getInstance().setTzOffset(0);  // restore
}

TEST(SystemClockProd, StartOfDayMasks) {
    /* 1700000000 = 2023-11-14 22:13:20 UTC; start of day = 1699920000 */
    EXPECT_EQ(SystemClock::startOfDay(1700000000u), 1699920000u);
}

TEST(SystemClockProd, ToHmsDecodesEpoch) {
    uint8_t h, m, s;
    SystemClock::toHMS(1700000000u, h, m, s);
    EXPECT_EQ(h, 22);
    EXPECT_EQ(m, 13);
    EXPECT_EQ(s, 20);
}

TEST(SystemClockProd, DateRoundTripViaCivilCalendar) {
    /* 2026-04-08 00:00 UTC */
    uint32_t epoch = SystemClock::dateToEpoch(20260408);
    uint32_t back = SystemClock::dateYYYYMMDD(epoch);
    EXPECT_EQ(back, 20260408u);
}

TEST(SystemClockProd, DateToEpochHandlesEarlyMonths) {
    /* February 29 2024 (leap year) */
    uint32_t epoch = SystemClock::dateToEpoch(20240229);
    EXPECT_EQ(SystemClock::dateYYYYMMDD(epoch), 20240229u);
    /* January 1 1970 */
    EXPECT_EQ(SystemClock::dateToEpoch(19700101), 0u);
}

// ── DisplayStatus toast / status helpers ────────────────────────────────────

TEST(DisplayStatusTest, HeaderBarDoesNotCrashWithoutDisplay) {
    resetGlobalDisplay(nullptr);
    dsp::headerBar("title");
    SUCCEED();
}

TEST(DisplayStatusTest, HeaderBarDrawsWithDisplay) {
    StubDisplay d; resetGlobalDisplay(&d);
    dsp::headerBar("ARCANA");
    EXPECT_GT(d.fillRectCalls, 0u);
    EXPECT_GT(d.drawStringCalls, 0u);
    resetGlobalDisplay(nullptr);
}

TEST(DisplayStatusTest, StatusLineDrawsAndClears) {
    StubDisplay d; resetGlobalDisplay(&d);
    dsp::statusLine("OK");
    EXPECT_GT(d.fillRectCalls, 0u);
    EXPECT_GT(d.drawStringCalls, 0u);

    uint32_t before = d.fillRectCalls;
    dsp::clearStatusLine();
    EXPECT_GT(d.fillRectCalls, before);
    resetGlobalDisplay(nullptr);
}

TEST(DisplayStatusTest, StatusLineNoOpWhenDisplayNull) {
    resetGlobalDisplay(nullptr);
    dsp::statusLine("hi");
    dsp::clearStatusLine();
    SUCCEED();
}

TEST(DisplayStatusTest, RequestToastCopiesMessage) {
    resetToast();
    dsp::requestToast("hello", /*durationMs=*/1000);
    EXPECT_TRUE(dsp::toastRequest().pending);
    EXPECT_STREQ(dsp::toastRequest().text, "hello");
    EXPECT_EQ(dsp::toastRequest().durationMs, 1000u);
    resetToast();
}

TEST(DisplayStatusTest, ToastUpdatePendingRendersBox) {
    resetToast();
    StubDisplay d; resetGlobalDisplay(&d);
    dsp::requestToast("test toast", 5000);
    bool needsRedraw = dsp::toastUpdate(/*currentTick=*/100);
    EXPECT_FALSE(needsRedraw);
    EXPECT_TRUE(dsp::toastState().active);
    EXPECT_GT(d.fillRectCalls, 0u);
    EXPECT_GT(d.drawHLineCalls, 0u);
    EXPECT_GT(d.drawStringCalls, 0u);

    /* Subsequent update with active toast → redraws */
    uint32_t before = d.fillRectCalls;
    dsp::toastUpdate(200);
    EXPECT_GT(d.fillRectCalls, before);

    resetToast();
    resetGlobalDisplay(nullptr);
}

TEST(DisplayStatusTest, ToastExpiryClearsBoxAndReturnsRedraw) {
    resetToast();
    StubDisplay d; resetGlobalDisplay(&d);
    dsp::requestToast("expire me", 100);
    dsp::toastUpdate(0);   // arms toast, dismissTick = 100

    /* currentTick >= dismissTick → expire */
    bool needsRedraw = dsp::toastUpdate(200);
    EXPECT_TRUE(needsRedraw);
    EXPECT_FALSE(dsp::toastState().active);

    resetToast();
    resetGlobalDisplay(nullptr);
}

TEST(DisplayStatusTest, DismissToastClearsActiveBox) {
    resetToast();
    StubDisplay d; resetGlobalDisplay(&d);
    dsp::requestToast("dismiss me", 5000);
    dsp::toastUpdate(0);   // arms toast

    dsp::dismissToast();
    bool needsRedraw = dsp::toastUpdate(50);
    EXPECT_TRUE(needsRedraw);
    EXPECT_FALSE(dsp::toastState().active);

    resetToast();
    resetGlobalDisplay(nullptr);
}

TEST(DisplayStatusTest, ToastUpdateIdleReturnsFalse) {
    resetToast();
    StubDisplay d; resetGlobalDisplay(&d);
    /* No pending, no active → returns false, no draw */
    EXPECT_FALSE(dsp::toastUpdate(0));
    resetGlobalDisplay(nullptr);
}

TEST(DisplayStatusTest, BackwardCompatToastWrappers) {
    resetToast();
    /* These wrap requestToast / dismissToast */
    dsp::toast("legacy", 1000, /*tick*/0);
    EXPECT_TRUE(dsp::toastRequest().pending);
    dsp::clearToast();
    EXPECT_TRUE(dsp::toastRequest().dismiss);
    resetToast();
}

TEST(DisplayStatusTest, ToastRedrawNoOpWhenDisplayNull) {
    resetGlobalDisplay(nullptr);
    dsp::ToastState ts{};
    ts.x = 0; ts.y = 0; ts.w = 100; ts.h = 50;
    dsp::toastRedraw(ts, "x");
    SUCCEED();
}

// ── BitmapButton ────────────────────────────────────────────────────────────

namespace {
int sTapCount = 0;
int sLongPressCount = 0;
void onTapCb(void*) { ++sTapCount; }
void onLongPressCb(void*) { ++sLongPressCount; }
} // anonymous

TEST(BitmapButtonTest, SetupAndDraw) {
    sTapCount = 0; sLongPressCount = 0;
    StubDisplay d;
    static const uint8_t bmp[8] = {0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF};
    dsp::BitmapButton btn;
    btn.setup(/*x*/10, /*y*/20, /*w*/8, /*h*/8,
              bmp, /*pressed*/nullptr,
              dsp::colors::WHITE, dsp::colors::BLACK,
              onTapCb, onLongPressCb, nullptr);
    btn.draw(d);
    EXPECT_EQ(d.drawXBitmapCalls, 1u);
    EXPECT_EQ(d.lastBitmapW, 8);
}

TEST(BitmapButtonTest, DrawNoOpWhenBitmapNull) {
    StubDisplay d;
    dsp::BitmapButton btn;
    /* No setup → bitmap stays null */
    btn.draw(d);
    EXPECT_EQ(d.drawXBitmapCalls, 0u);
}

TEST(BitmapButtonTest, TouchDownAndUpCallsTap) {
    sTapCount = 0;
    StubDisplay d;
    static const uint8_t bmp[8] = {0};
    static const uint8_t bmpPressed[8] = {0xFF};
    dsp::BitmapButton btn;
    btn.setup(0, 0, 100, 100, bmp, bmpPressed,
              dsp::colors::WHITE, dsp::colors::BLACK,
              onTapCb, onLongPressCb, nullptr);

    dsp::TouchEvent down{}; down.type = dsp::TouchEvent::Down;
    down.x = 50; down.y = 50;
    EXPECT_TRUE(btn.handleTouch(d, down, /*tick*/100));

    dsp::TouchEvent up{}; up.type = dsp::TouchEvent::Up;
    up.x = 50; up.y = 50;
    EXPECT_TRUE(btn.handleTouch(d, up, /*tick*/200));   // 100ms < LONG_PRESS_MS=500

    EXPECT_EQ(sTapCount, 1);
    EXPECT_EQ(sLongPressCount, 0);
}

TEST(BitmapButtonTest, LongPressFiresLongPressCallback) {
    sTapCount = 0; sLongPressCount = 0;
    StubDisplay d;
    static const uint8_t bmp[8] = {0};
    dsp::BitmapButton btn;
    btn.setup(0, 0, 100, 100, bmp, nullptr,
              dsp::colors::WHITE, dsp::colors::BLACK,
              onTapCb, onLongPressCb, nullptr);

    dsp::TouchEvent down{}; down.type = dsp::TouchEvent::Down;
    down.x = 10; down.y = 10;
    btn.handleTouch(d, down, /*tick*/0);

    dsp::TouchEvent up{}; up.type = dsp::TouchEvent::Up;
    up.x = 10; up.y = 10;
    btn.handleTouch(d, up, /*tick*/600);  // 600ms >= 500 → long-press

    EXPECT_EQ(sLongPressCount, 1);
    EXPECT_EQ(sTapCount, 0);
}

TEST(BitmapButtonTest, MoveOutsideHitRectCancels) {
    sTapCount = 0;
    StubDisplay d;
    static const uint8_t bmp[8] = {0};
    dsp::BitmapButton btn;
    btn.setup(/*x*/10, /*y*/10, /*w*/20, /*h*/20, bmp, nullptr,
              dsp::colors::WHITE, dsp::colors::BLACK,
              onTapCb, onLongPressCb, nullptr);

    dsp::TouchEvent down{}; down.type = dsp::TouchEvent::Down;
    down.x = 15; down.y = 15;
    btn.handleTouch(d, down, /*tick*/0);

    /* Move OUTSIDE the hit rect → mPressed cleared, returns false */
    dsp::TouchEvent move{}; move.type = dsp::TouchEvent::Move;
    move.x = 100; move.y = 100;
    bool consumed = btn.handleTouch(d, move, /*tick*/50);
    EXPECT_FALSE(consumed);

    /* Subsequent up does NOT fire tap (mPressed already false) */
    dsp::TouchEvent up{}; up.type = dsp::TouchEvent::Up;
    up.x = 100; up.y = 100;
    btn.handleTouch(d, up, /*tick*/100);
    EXPECT_EQ(sTapCount, 0);
}

TEST(BitmapButtonTest, MoveStillInsideKeepsPressed) {
    StubDisplay d;
    static const uint8_t bmp[8] = {0};
    dsp::BitmapButton btn;
    btn.setup(/*x*/10, /*y*/10, /*w*/20, /*h*/20, bmp, nullptr,
              dsp::colors::WHITE, dsp::colors::BLACK,
              onTapCb, onLongPressCb, nullptr);

    dsp::TouchEvent down{}; down.type = dsp::TouchEvent::Down;
    down.x = 15; down.y = 15;
    btn.handleTouch(d, down, 0);

    dsp::TouchEvent move{}; move.type = dsp::TouchEvent::Move;
    move.x = 20; move.y = 20;  // still inside
    EXPECT_TRUE(btn.handleTouch(d, move, 50));
}

TEST(BitmapButtonTest, OnKeyKey1PressFiresTap) {
    sTapCount = 0;
    StubDisplay d;
    static const uint8_t bmp[8] = {0};
    dsp::BitmapButton btn;
    btn.setup(0, 0, 100, 100, bmp, nullptr,
              dsp::colors::WHITE, dsp::colors::BLACK,
              onTapCb, onLongPressCb, nullptr);

    dsp::KeyEvent ev{};
    ev.type = dsp::KeyEvent::Press;
    ev.key  = dsp::KeyEvent::Key1;
    EXPECT_TRUE(btn.onKey(d, ev));
    EXPECT_EQ(sTapCount, 1);
}

TEST(BitmapButtonTest, OnKeyKey1LongPressFiresLongCb) {
    sLongPressCount = 0;
    StubDisplay d;
    static const uint8_t bmp[8] = {0};
    dsp::BitmapButton btn;
    btn.setup(0, 0, 100, 100, bmp, nullptr,
              dsp::colors::WHITE, dsp::colors::BLACK,
              onTapCb, onLongPressCb, nullptr);

    dsp::KeyEvent ev{};
    ev.type = dsp::KeyEvent::LongPress;
    ev.key  = dsp::KeyEvent::Key1;
    EXPECT_TRUE(btn.onKey(d, ev));
    EXPECT_EQ(sLongPressCount, 1);
}

TEST(BitmapButtonTest, OnKeyOtherKeyIgnored) {
    StubDisplay d;
    static const uint8_t bmp[8] = {0};
    dsp::BitmapButton btn;
    btn.setup(0, 0, 100, 100, bmp, nullptr,
              dsp::colors::WHITE, dsp::colors::BLACK,
              onTapCb, onLongPressCb, nullptr);

    dsp::KeyEvent ev{};
    ev.type = dsp::KeyEvent::Press;
    ev.key  = dsp::KeyEvent::Key2;
    EXPECT_FALSE(btn.onKey(d, ev));
}
