/**
 * @file test_widgets.cpp
 * @brief Coverage for FormWidgets + DialogWidgets + Widget base + WidgetGroup.
 *
 * Provides a no-op IDisplay implementation that records draw calls so tests
 * can assert widgets exercise their full draw + handleTouch + onKey paths
 * without needing real LCD hardware.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "IDisplay.hpp"
#include "Widget.hpp"
#include "FormWidgets.hpp"
#include "DialogWidgets.hpp"

using arcana::display::IDisplay;
using arcana::display::Color;
using arcana::display::Widget;
using arcana::display::WidgetGroup;
using arcana::display::Label;
using arcana::display::Checkbox;
using arcana::display::RadioGroup;
using arcana::display::Slider;
using arcana::display::ProgressBar;
using arcana::display::NumberStepper;
using arcana::display::AlertDialog;
using arcana::display::ConfirmDialog;
using arcana::display::Toast;
using arcana::display::TouchEvent;
using arcana::display::KeyEvent;
namespace colors = arcana::display::colors;

namespace {

/* Stub display — no-op renders, but tracks call counts so tests can verify
 * the widget actually drew something. */
class StubDisplay : public IDisplay {
public:
    uint16_t mWidth  = 240;
    uint16_t mHeight = 320;
    uint32_t fillRectCalls   = 0;
    uint32_t drawStringCalls = 0;

    uint16_t width() const override  { return mWidth; }
    uint16_t height() const override { return mHeight; }
    void fillScreen(Color) override {}
    void fillRect(uint16_t, uint16_t, uint16_t, uint16_t, Color) override {
        ++fillRectCalls;
    }
    void drawChar(uint16_t, uint16_t, char, Color, Color, uint8_t) override {}
    void drawString(uint16_t, uint16_t, const char*, Color, Color, uint8_t) override {
        ++drawStringCalls;
    }
    void drawXBitmap(uint16_t, uint16_t, uint16_t, uint16_t,
                     const uint8_t*, Color, Color) override {}
};

TouchEvent makeTouch(TouchEvent::Type t, uint16_t x, uint16_t y) {
    return { t, x, y };
}

KeyEvent makeKey(KeyEvent::Type t, KeyEvent::Key k) {
    return { t, k };
}

} // anonymous namespace

// ── Widget base class ──────────────────────────────────────────────────────

TEST(WidgetBase, HitTestRespectsBoundsAndEnabled) {
    Label label;
    label.x = 10; label.y = 20; label.w = 50; label.h = 30;
    label.enabled = true; label.visible = true;

    EXPECT_TRUE(label.hitTest(15, 25));
    EXPECT_TRUE(label.hitTest(10, 20));
    EXPECT_FALSE(label.hitTest(9, 20));
    EXPECT_FALSE(label.hitTest(60, 50));

    label.enabled = false;
    EXPECT_FALSE(label.hitTest(15, 25));
    label.enabled = true; label.visible = false;
    EXPECT_FALSE(label.hitTest(15, 25));
}

TEST(WidgetBase, DrawFocusBorder) {
    StubDisplay disp;
    Label label;
    label.x = 10; label.y = 20; label.w = 30; label.h = 10;
    label.drawFocus(disp, true);
    EXPECT_GT(disp.fillRectCalls, 0u);
}

TEST(WidgetBase, DefaultHandleTouchAndKeyAreNoOp) {
    StubDisplay disp;
    Widget* w = nullptr;  /* base is abstract; use Label which inherits defaults */
    Label label;
    w = &label;
    EXPECT_FALSE(w->handleTouch(disp, makeTouch(TouchEvent::Down, 0, 0), 0));
    EXPECT_FALSE(w->onKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key1)));
}

// ── Label ──────────────────────────────────────────────────────────────────

TEST(LabelTest, SetupAndDraw) {
    StubDisplay disp;
    Label label;
    label.setup(5, 10, "Hello");
    EXPECT_EQ(label.x, 5u);
    EXPECT_EQ(label.y, 10u);
    EXPECT_FALSE(label.focusable);

    label.draw(disp);
    EXPECT_EQ(disp.drawStringCalls, 1u);
}

TEST(LabelTest, SetTextWithClear) {
    StubDisplay disp;
    Label label;
    label.setup(5, 10, "Old");
    label.setText(disp, "New", /*clearW=*/40);
    EXPECT_STREQ(label.text, "New");
    EXPECT_GE(disp.fillRectCalls, 1u);  /* clearW > 0 → fillRect called */
    EXPECT_GE(disp.drawStringCalls, 1u);
}

TEST(LabelTest, SetTextWithoutClear) {
    StubDisplay disp;
    Label label;
    label.setup(0, 0, "x");
    uint32_t before = disp.fillRectCalls;
    label.setText(disp, "y", /*clearW=*/0);
    EXPECT_EQ(disp.fillRectCalls, before);  /* clearW == 0 → no clear */
}

// ── Checkbox ───────────────────────────────────────────────────────────────

TEST(CheckboxTest, SetupDrawAndState) {
    StubDisplay disp;
    Checkbox cb;
    bool changed = false;
    bool lastValue = false;
    cb.setup(20, 30, "Enable",
             [](bool v, void* ctx) { *(bool*)ctx = true; *((bool*)ctx + 1) = v; },
             nullptr, false);
    /* The lambda above won't work without ctx capturing both flags;
     * use a struct via the C-style callback. */
    struct Cb { bool changed = false; bool last = false; } state;
    cb.setup(20, 30, "Enable",
             [](bool v, void* ctx) {
                 auto* s = static_cast<Cb*>(ctx);
                 s->changed = true;
                 s->last = v;
             },
             &state, false);
    cb.draw(disp);
    EXPECT_GT(disp.fillRectCalls, 0u);
}

TEST(CheckboxTest, TouchTogglesState) {
    StubDisplay disp;
    Checkbox cb;
    struct Cb { bool changed = false; bool last = false; } state;
    cb.setup(0, 0, nullptr,
             [](bool v, void* ctx) {
                 auto* s = static_cast<Cb*>(ctx);
                 s->changed = true; s->last = v;
             },
             &state, false);

    /* Touch up inside the box → toggle to true */
    EXPECT_TRUE(cb.handleTouch(disp, makeTouch(TouchEvent::Up, 5, 5), 0));
    EXPECT_TRUE(cb.checked);
    EXPECT_TRUE(state.changed);
    EXPECT_TRUE(state.last);

    /* Touch down doesn't toggle but consumes */
    state.changed = false;
    EXPECT_TRUE(cb.handleTouch(disp, makeTouch(TouchEvent::Down, 5, 5), 0));
    EXPECT_FALSE(state.changed);

    /* Touch up outside doesn't toggle */
    state.changed = false;
    EXPECT_FALSE(cb.handleTouch(disp, makeTouch(TouchEvent::Up, 100, 100), 0));
    EXPECT_FALSE(state.changed);
}

TEST(CheckboxTest, Key1Toggles) {
    StubDisplay disp;
    Checkbox cb;
    cb.setup(0, 0, "lbl", nullptr, nullptr, false);
    EXPECT_TRUE(cb.onKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key1)));
    EXPECT_TRUE(cb.checked);
    EXPECT_FALSE(cb.onKey(disp, makeKey(KeyEvent::Release, KeyEvent::Key1)));
}

// ── RadioGroup ─────────────────────────────────────────────────────────────

TEST(RadioGroupTest, SetupAndDraw) {
    StubDisplay disp;
    RadioGroup rg;
    const char* opts[] = { "A", "B", "C" };
    rg.setup(0, 0, opts, 3, nullptr, nullptr, 1);
    EXPECT_EQ(rg.optionCount, 3u);
    EXPECT_EQ(rg.selected, 1u);
    rg.draw(disp);
    EXPECT_GT(disp.fillRectCalls, 0u);
}

TEST(RadioGroupTest, ClampOptionCountToMax) {
    StubDisplay disp;
    RadioGroup rg;
    const char* opts[8] = { "1", "2", "3", "4", "5", "6", "7", "8" };
    rg.setup(0, 0, opts, 8, nullptr, nullptr);
    /* MAX_OPTIONS is 6 (compile-time constant) — value-init via copy avoids
     * the linker needing an out-of-class definition. */
    EXPECT_LE(rg.optionCount, static_cast<uint8_t>(6));
}

TEST(RadioGroupTest, TouchSelectsOption) {
    StubDisplay disp;
    RadioGroup rg;
    const char* opts[] = { "A", "B", "C" };
    struct St { uint8_t lastIdx = 99; bool fired = false; } st;
    rg.setup(0, 0, opts, 3,
             [](uint8_t idx, void* ctx) {
                 auto* s = static_cast<St*>(ctx);
                 s->lastIdx = idx; s->fired = true;
             }, &st, 0);

    /* Tap option 2 (y in [32, 48)) */
    EXPECT_TRUE(rg.handleTouch(disp, makeTouch(TouchEvent::Up, 5, 33), 0));
    EXPECT_EQ(rg.selected, 2u);
    EXPECT_TRUE(st.fired);
    EXPECT_EQ(st.lastIdx, 2);

    /* Tap same option → no callback fired (idx == selected) */
    st.fired = false;
    rg.handleTouch(disp, makeTouch(TouchEvent::Up, 5, 33), 0);
    EXPECT_FALSE(st.fired);
}

// ── Slider ─────────────────────────────────────────────────────────────────

TEST(SliderTest, SetupAndDraw) {
    StubDisplay disp;
    Slider s;
    s.setup(0, 0, /*w=*/100, /*min=*/0, /*max=*/100, /*initial=*/50,
            nullptr, nullptr);
    s.draw(disp);
    EXPECT_GT(disp.fillRectCalls, 0u);
}

TEST(SliderTest, TouchUpdatesValue) {
    StubDisplay disp;
    Slider s;
    int16_t lastV = -1;
    s.setup(0, 0, 100, 0, 100, 0,
            [](int16_t v, void* ctx) { *(int16_t*)ctx = v; }, &lastV);
    /* Touch at x=50 → roughly midpoint */
    s.handleTouch(disp, makeTouch(TouchEvent::Down, 50, 8), 0);
    EXPECT_GT(s.value, 0);
    /* Touch at far right */
    s.handleTouch(disp, makeTouch(TouchEvent::Move, 200, 8), 0);
    EXPECT_EQ(s.value, 100);
    /* Touch at far left */
    s.handleTouch(disp, makeTouch(TouchEvent::Move, 0, 8), 0);
    EXPECT_EQ(s.value, 0);
}

TEST(SliderTest, Key1Increments) {
    StubDisplay disp;
    Slider s;
    s.setup(0, 0, 100, 0, 100, 50, nullptr, nullptr);
    s.onKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key1));
    EXPECT_GT(s.value, 50);
}

TEST(SliderTest, Key1IncrementClampsAtMax) {
    StubDisplay disp;
    Slider s;
    s.setup(0, 0, 100, 0, 100, 100, nullptr, nullptr);
    s.onKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key1));
    EXPECT_EQ(s.value, 100);
}

// ── ProgressBar ────────────────────────────────────────────────────────────

TEST(ProgressBarTest, DrawAtPercents) {
    StubDisplay disp;
    ProgressBar pb;
    pb.setup(0, 0, 100, 10);
    EXPECT_FALSE(pb.focusable);

    pb.setPercent(disp, 0);
    EXPECT_EQ(pb.percent, 0);

    pb.setPercent(disp, 50);
    EXPECT_EQ(pb.percent, 50);

    pb.setPercent(disp, 100);
    EXPECT_EQ(pb.percent, 100);

    pb.setPercent(disp, 200);  /* clamped */
    EXPECT_EQ(pb.percent, 100);
}

// ── NumberStepper ──────────────────────────────────────────────────────────

TEST(NumberStepperTest, SetupAndDraw) {
    StubDisplay disp;
    NumberStepper ns;
    ns.setup(0, 0, 0, 100, 50, 5, nullptr, nullptr);
    EXPECT_EQ(ns.value, 50);
    ns.draw(disp);
    EXPECT_GT(disp.drawStringCalls, 0u);
}

TEST(NumberStepperTest, TouchPlusButtonIncrements) {
    StubDisplay disp;
    NumberStepper ns;
    int16_t last = -1;
    ns.setup(0, 0, 0, 100, 50, 5,
             [](int16_t v, void* ctx) { *(int16_t*)ctx = v; }, &last);

    /* + button is at x=BTN_W+VAL_W=60, y=0..16. Tap at x=70 */
    EXPECT_TRUE(ns.handleTouch(disp, makeTouch(TouchEvent::Up, 70, 8), 0));
    EXPECT_EQ(ns.value, 55);
    EXPECT_EQ(last, 55);
}

TEST(NumberStepperTest, TouchMinusButtonDecrements) {
    StubDisplay disp;
    NumberStepper ns;
    ns.setup(0, 0, 0, 100, 50, 5, nullptr, nullptr);
    /* - button is at x=0..BTN_W=20 */
    EXPECT_TRUE(ns.handleTouch(disp, makeTouch(TouchEvent::Up, 5, 8), 0));
    EXPECT_EQ(ns.value, 45);
}

TEST(NumberStepperTest, TouchValueAreaNoOp) {
    StubDisplay disp;
    NumberStepper ns;
    ns.setup(0, 0, 0, 100, 50, 5, nullptr, nullptr);
    /* Middle area (x in [BTN_W, BTN_W+VAL_W) = [20,60)) → no-op */
    EXPECT_FALSE(ns.handleTouch(disp, makeTouch(TouchEvent::Up, 30, 8), 0));
    EXPECT_EQ(ns.value, 50);
}

TEST(NumberStepperTest, IncrementClampsAtMax) {
    StubDisplay disp;
    NumberStepper ns;
    ns.setup(0, 0, 0, 100, 98, 5, nullptr, nullptr);
    ns.handleTouch(disp, makeTouch(TouchEvent::Up, 70, 8), 0);
    EXPECT_EQ(ns.value, 100);  /* clamped */
}

TEST(NumberStepperTest, DecrementClampsAtMin) {
    StubDisplay disp;
    NumberStepper ns;
    ns.setup(0, 0, 0, 100, 2, 5, nullptr, nullptr);
    ns.handleTouch(disp, makeTouch(TouchEvent::Up, 5, 8), 0);
    EXPECT_EQ(ns.value, 0);  /* clamped */
}

TEST(NumberStepperTest, Key1IncrementsKey1LongPressDecrements) {
    StubDisplay disp;
    NumberStepper ns;
    ns.setup(0, 0, 0, 100, 50, 5, nullptr, nullptr);
    EXPECT_TRUE(ns.onKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key1)));
    EXPECT_EQ(ns.value, 55);
    EXPECT_TRUE(ns.onKey(disp, makeKey(KeyEvent::LongPress, KeyEvent::Key1)));
    EXPECT_EQ(ns.value, 50);
}

// ── WidgetGroup ────────────────────────────────────────────────────────────

TEST(WidgetGroupTest, AddAndDrawAll) {
    StubDisplay disp;
    WidgetGroup grp;
    Label l1, l2;
    l1.setup(0, 0, "A");
    l2.setup(0, 20, "B");
    grp.add(&l1);
    grp.add(&l2);
    grp.drawAll(disp);
    EXPECT_GE(disp.drawStringCalls, 2u);
}

TEST(WidgetGroupTest, FocusNavigatesFocusableOnly) {
    StubDisplay disp;
    WidgetGroup grp;
    Label readonly_;        // not focusable
    Checkbox cb1, cb2;       // focusable
    readonly_.setup(0, 0, "ro");
    cb1.setup(0, 20, "1", nullptr, nullptr, false);
    cb2.setup(0, 40, "2", nullptr, nullptr, false);
    grp.add(&readonly_);
    grp.add(&cb1);
    grp.add(&cb2);

    EXPECT_EQ(grp.focusIndex(), -1);
    grp.focusNext(disp);
    EXPECT_EQ(grp.focusIndex(), 1);  /* skipped readonly_ */
    grp.focusNext(disp);
    EXPECT_EQ(grp.focusIndex(), 2);
    grp.focusNext(disp);
    EXPECT_EQ(grp.focusIndex(), 1);  /* wraps back to first focusable */
}

TEST(WidgetGroupTest, FocusPrevWraps) {
    StubDisplay disp;
    WidgetGroup grp;
    Checkbox cb1, cb2;
    cb1.setup(0, 0, "1", nullptr, nullptr, false);
    cb2.setup(0, 20, "2", nullptr, nullptr, false);
    grp.add(&cb1);
    grp.add(&cb2);

    grp.focusNext(disp);
    EXPECT_EQ(grp.focusIndex(), 0);
    grp.focusPrev(disp);
    EXPECT_EQ(grp.focusIndex(), 1);  /* wrap */
    grp.focusPrev(disp);
    EXPECT_EQ(grp.focusIndex(), 0);
}

TEST(WidgetGroupTest, HandleKeyKey2NavigatesFocus) {
    StubDisplay disp;
    WidgetGroup grp;
    Checkbox cb1, cb2;
    cb1.setup(0, 0, nullptr, nullptr, nullptr, false);
    cb2.setup(0, 20, nullptr, nullptr, nullptr, false);
    grp.add(&cb1);
    grp.add(&cb2);

    EXPECT_TRUE(grp.handleKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key2)));
    EXPECT_EQ(grp.focusIndex(), 0);
    EXPECT_TRUE(grp.handleKey(disp, makeKey(KeyEvent::LongPress, KeyEvent::Key2)));
    EXPECT_EQ(grp.focusIndex(), 1);
}

TEST(WidgetGroupTest, HandleKeyForwardsToFocusedWidget) {
    StubDisplay disp;
    WidgetGroup grp;
    Checkbox cb;
    cb.setup(0, 0, nullptr, nullptr, nullptr, false);
    grp.add(&cb);
    grp.handleKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key2));
    /* Key1 → forwards to checkbox → toggles */
    grp.handleKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key1));
    EXPECT_TRUE(cb.checked);
}

TEST(WidgetGroupTest, HandleTouchRoutesToHitWidget) {
    StubDisplay disp;
    WidgetGroup grp;
    Checkbox cb;
    cb.setup(50, 50, nullptr, nullptr, nullptr, false);
    grp.add(&cb);

    /* Touch down at (55,55) → cb hit-tests true → consumed by cb */
    EXPECT_TRUE(grp.handleTouch(disp, makeTouch(TouchEvent::Down, 55, 55), 0));
    /* Subsequent up → also routed to cb (still pressed) → toggles */
    grp.handleTouch(disp, makeTouch(TouchEvent::Up, 55, 55), 0);
    EXPECT_TRUE(cb.checked);

    /* Touch in empty space → no hit */
    Checkbox cb2;
    cb2.setup(200, 200, nullptr, nullptr, nullptr, false);
    grp.add(&cb2);
    EXPECT_FALSE(grp.handleTouch(disp, makeTouch(TouchEvent::Down, 5, 5), 0));
}

// ── AlertDialog ────────────────────────────────────────────────────────────

TEST(AlertDialogTest, ShowDrawsAndIsActive) {
    StubDisplay disp;
    AlertDialog dlg;
    EXPECT_FALSE(dlg.active);
    dlg.show(disp, "Title", "Message");
    EXPECT_TRUE(dlg.active);
    EXPECT_GT(disp.drawStringCalls, 0u);
}

TEST(AlertDialogTest, TouchOnOkButtonDismisses) {
    StubDisplay disp;
    AlertDialog dlg;
    bool dismissed = false;
    dlg.show(disp, "T", "M",
             [](void* ctx) { *(bool*)ctx = true; }, &dismissed);

    /* OK button: dialog x=(240-200)/2=20, y=(320-80)/2=120;
     * mBtnX = 20 + (200-60)/2 = 90, mBtnY = 120+80-20-6=174 */
    EXPECT_TRUE(dlg.handleTouch(disp, makeTouch(TouchEvent::Up, 110, 180)));
    EXPECT_FALSE(dlg.active);
    EXPECT_TRUE(dismissed);
}

TEST(AlertDialogTest, Key1Dismisses) {
    StubDisplay disp;
    AlertDialog dlg;
    bool dismissed = false;
    dlg.show(disp, "T", "M",
             [](void* ctx) { *(bool*)ctx = true; }, &dismissed);
    EXPECT_TRUE(dlg.handleKey(disp,
        makeKey(KeyEvent::Press, KeyEvent::Key1)));
    EXPECT_FALSE(dlg.active);
    EXPECT_TRUE(dismissed);
}

TEST(AlertDialogTest, InactiveIgnoresEvents) {
    StubDisplay disp;
    AlertDialog dlg;
    EXPECT_FALSE(dlg.handleTouch(disp, makeTouch(TouchEvent::Up, 0, 0)));
    EXPECT_FALSE(dlg.handleKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key1)));
}

// ── ConfirmDialog ──────────────────────────────────────────────────────────

TEST(ConfirmDialogTest, ShowAndKeyToggle) {
    StubDisplay disp;
    ConfirmDialog dlg;
    bool fired = false;
    bool result = false;
    struct R { bool& fired; bool& result; };
    R ref = { fired, result };

    dlg.show(disp, "Title", "Are you sure?",
             [](bool ok, void* ctx) {
                 R* r = static_cast<R*>(ctx);
                 r->fired = true; r->result = ok;
             }, &ref);
    EXPECT_TRUE(dlg.active);
    EXPECT_EQ(dlg.focusBtn, 1);  /* OK by default */

    /* Key2 → toggle to Cancel */
    dlg.handleKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key2));
    EXPECT_EQ(dlg.focusBtn, 0);

    /* Key1 → confirm with current focus (Cancel) */
    dlg.handleKey(disp, makeKey(KeyEvent::Press, KeyEvent::Key1));
    EXPECT_FALSE(dlg.active);
    EXPECT_TRUE(fired);
    EXPECT_FALSE(result);
}

TEST(ConfirmDialogTest, TouchOnOkButton) {
    StubDisplay disp;
    ConfirmDialog dlg;
    bool fired = false;
    bool result = false;
    struct R { bool& fired; bool& result; };
    R ref = { fired, result };
    dlg.show(disp, "T", "M",
             [](bool ok, void* ctx) {
                 R* r = static_cast<R*>(ctx);
                 r->fired = true; r->result = ok;
             }, &ref);

    /* OK button is at right side. Compute mBtnY from the dialog draw:
     * x = (240-200)/2 = 20; y = (320-80)/2 = 120;
     * mBtnY = y + DLG_H - BTN_H - 6 = 120 + 80 - 20 - 6 = 174.
     * mOkX = 20 + 200/2 + 10 = 130. */
    dlg.handleTouch(disp, makeTouch(TouchEvent::Up, 135, 175));
    EXPECT_FALSE(dlg.active);
    EXPECT_TRUE(fired);
    EXPECT_TRUE(result);
}

TEST(ConfirmDialogTest, TouchOnCancelButton) {
    StubDisplay disp;
    ConfirmDialog dlg;
    bool fired = false;
    bool result = true;
    struct R { bool& fired; bool& result; };
    R ref = { fired, result };
    dlg.show(disp, "T", "M",
             [](bool ok, void* ctx) {
                 R* r = static_cast<R*>(ctx);
                 r->fired = true; r->result = ok;
             }, &ref);
    /* mCancelX = 20 + 200/2 - 50 - 10 = 60. */
    dlg.handleTouch(disp, makeTouch(TouchEvent::Up, 65, 175));
    EXPECT_FALSE(dlg.active);
    EXPECT_TRUE(fired);
    EXPECT_FALSE(result);
}

// ── Toast ──────────────────────────────────────────────────────────────────

TEST(ToastTest, ShowAndAutoDismiss) {
    StubDisplay disp;
    Toast toast;
    toast.show(disp, "Hello", 1000, 0);
    EXPECT_TRUE(toast.active);
    EXPECT_FALSE(toast.update(disp, 500));   /* not yet expired */
    EXPECT_TRUE(toast.update(disp, 1000));   /* exactly expired */
    EXPECT_FALSE(toast.active);
    EXPECT_FALSE(toast.update(disp, 2000));  /* already inactive */
}
