# Cross-Platform Display Abstraction Layer

## Context

View code is tightly coupled to `Ili9341Lcd` — every render method takes `Ili9341Lcd&` directly. Three service files create throwaway `Ili9341Lcd` instances for ad-hoc status lines, each with slightly different implementations. Goal: **View code never changes when swapping display hardware**, and ad-hoc LCD usage is unified for visual consistency.

User request: Decorator Pattern also handles aesthetics + consistency (not just thread safety).
Additional: `drawXBitmap` for icon/bitmap rendering (like u8g2_DrawXBMP), touch event + gesture interfaces, physical key focus navigation (光棒), complete form widget system (HTML form elements mapped to embedded LCD).

## Architecture

```
Output (Display)                  Input (Touch — future)     Input (Keys — now)
─────────────                     ──────────────────────     ──────────────────
Controller (wiring)               ITouch ← Xpt2046Touch     KEY1(PA0) KEY2(PC13)
     │                                │                           │
Ili9341Lcd ──► Ili9341Display ──► MutexDisplay ──► IDisplay*      KeyEvent
(HW driver)     (Adapter)        (Decorator)        │               │
                                                ┌───┴───────────────┘
                                           ViewManager
                                        (dispatchTouch / dispatchKey
                                         + view switch + gesture nav)
                                               │
                                    ┌──────────┴──────────┐
                                 MainView            DisplayStatus
                              (Template Method)    (shared utility)
                                    │
                              WidgetGroup
                           (focus 光棒 + hit-test)
                              ┌──┬──┬──┬──┐
                           [Btn][Chk][Sld][Rad]...
                              onTouch / onKey
```

## New Files (all header-only, no build system .cpp changes)

| # | File | Location | Purpose |
|---|------|----------|---------|
| 0 | `DisplayConfig.hpp` | `Shared/Inc/display/` | Feature flags — compile-time on/off for each module (zero cost when off) |
| 1 | `IDisplay.hpp` | `Shared/Inc/display/` | Interface + Color type + color constants + `drawXBitmap` + `g_display` extern |
| 2 | `TouchTypes.hpp` | `Shared/Inc/display/` | TouchEvent + Gesture enums (data types only, no driver) |
| 3 | `MutexDisplay.hpp` | `Shared/Inc/display/` | Decorator: thread safety wrapping any IDisplay |
| 4 | `Widget.hpp` | `Shared/Inc/display/` | Base Widget class + WidgetGroup (manages heterogeneous widgets) |
| 5 | `BitmapButton.hpp` | `Shared/Inc/display/` | BitmapButton : Widget — XBM icon button, tap/long-press |
| 6 | `FormWidgets.hpp` | `Shared/Inc/display/` | Checkbox, RadioGroup, Slider, ProgressBar, Label, NumberStepper |
| 7 | `DialogWidgets.hpp` | `Shared/Inc/display/` | AlertDialog, ConfirmDialog, Toast overlay widgets |
| 8 | `DisplayStatus.hpp` | `Shared/Inc/display/` | Utility: statusLine + header bar + toast auto-dismiss |
| 9 | `Ili9341Display.hpp` | `Services/Driver/` | Adapter: wraps Ili9341Lcd → IDisplay (incl. drawXBitmap FSMC-optimized) |
| 10 | `ViewManager.hpp` | `Services/View/` | Front Controller: view switching + touch/key event routing |

## Files to Modify

| # | File | Change |
|---|------|--------|
| 7 | `Debug/Services/subdir.mk` | Add `-I../../../Shared/Inc/display` to SERVICES_INCLUDES |
| 8 | `Ili9341Lcd.hpp` | Add `drawXBitmap()` declaration |
| 9 | `Ili9341Lcd.cpp` | Add `drawXBitmap()` implementation (FSMC-optimized) |
| 10 | `LcdView.hpp` | `Ili9341Lcd&` → `IDisplay&`, add `onTouch()` / `onGesture()` virtual methods |
| 11 | `MainView.hpp` | `Ili9341Lcd*` → `display::IDisplay*` in Input + method signatures |
| 12 | `MainView.cpp` | `Ili9341Lcd::COLOR` → `display::colors::COLOR` (~25 replacements) |
| 13 | `EcgBuffer.hpp` | Forward decl → `#include "IDisplay.hpp"`, `Ili9341Lcd*` → `display::IDisplay*` |
| 14 | `EcgDisplay.cpp` | `lcd::Ili9341Lcd::COLOR` → `display::colors::COLOR` |
| 15 | `LcdService.hpp` | `getLcd()` → `getDisplay()` returning `display::IDisplay&` |
| 16 | `LcdServiceImpl.hpp` | Add `Ili9341Display mAdapter`, return adapter from `getDisplay()` |
| 17 | `LcdServiceImpl.cpp` | Include adapter header, update `initHAL()` |
| 18 | `Controller.cpp` | Wire decorator chain, define `g_display`, use adapter for views |
| 19 | `AtsStorageServiceImpl.cpp` | Replace `#include "Ili9341Lcd.hpp"` + local `lcdStatus()` with `#include "DisplayStatus.hpp"` + `display::statusLine()` |
| 20 | `SdBenchmarkServiceImpl.cpp` | Same: replace `lcdMsg()` with `display::statusLine()` |
| 21 | `MqttServiceImpl.cpp` | Same: replace `lcdStatus()` with `display::statusLine()` |

## Detailed Design

### 0. DisplayConfig.hpp — `Shared/Inc/display/DisplayConfig.hpp`

Compile-time feature flags. `0` = code exists in source tree but is **not compiled** (zero Flash, zero RAM). Change to `1` when the feature is needed.

```cpp
#pragma once

// ═══════════════════════════════════════════════════
//  Display Abstraction Layer — Feature Flags
//
//  0 = code exists but NOT compiled (zero cost)
//  1 = compiled into binary
//
//  Only enable what you need — saves Flash + RAM.
// ═══════════════════════════════════════════════════

// ── Core (always on) ─────────────────────────────
#define DISPLAY_FEATURE_CORE        1   // IDisplay, Color, colors namespace, g_display

// ── Decorators / Utilities ───────────────────────
#define DISPLAY_FEATURE_MUTEX       1   // MutexDisplay — thread-safe decorator
#define DISPLAY_FEATURE_STATUS      1   // statusLine() / headerBar() / clearStatusLine()

// ── Input Types ──────────────────────────────────
#define DISPLAY_FEATURE_TOUCH       0   // TouchEvent, Gesture, TouchPoint
#define DISPLAY_FEATURE_KEY_NAV     0   // KeyEvent (physical buttons KEY1/KEY2)

// ── Widget System ────────────────────────────────
#define DISPLAY_FEATURE_WIDGETS     0   // Widget base class + WidgetGroup (focus 光棒)
#define DISPLAY_FEATURE_BUTTON      0   // BitmapButton (XBM icon, tap/longPress)
#define DISPLAY_FEATURE_FORM        0   // Label, Checkbox, RadioGroup, Slider,
                                        // ProgressBar, NumberStepper

// ── Dialogs / Overlays ───────────────────────────
#define DISPLAY_FEATURE_DIALOGS     0   // AlertDialog, ConfirmDialog, Toast

// ── Navigation ───────────────────────────────────
#define DISPLAY_FEATURE_NAV_STACK   0   // ViewManager push/pop multi-level navigation
```

**Dependencies** (auto-enforced via `#if` guards):

```
FEATURE_FORM     → requires FEATURE_WIDGETS
FEATURE_BUTTON   → requires FEATURE_WIDGETS
FEATURE_DIALOGS  → requires FEATURE_TOUCH (for handleTouch)
FEATURE_KEY_NAV  → requires FEATURE_TOUCH (shares TouchTypes.hpp)
FEATURE_WIDGETS  → requires FEATURE_TOUCH + FEATURE_KEY_NAV
FEATURE_NAV_STACK → requires FEATURE_TOUCH
```

**Each header wraps its content with the corresponding guard:**
```cpp
// FormWidgets.hpp
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_FORM
// ... all widget code ...
#endif

// TouchTypes.hpp
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_TOUCH
// ... TouchEvent, Gesture, KeyEvent ...
#endif
```

**Current default**: only Core + Mutex + Status = **Phase 1+2 only** (~500B Flash, ~20B RAM).
Enable more features as needed — source code is always in the tree.

### 1. IDisplay.hpp — `Shared/Inc/display/IDisplay.hpp`

```cpp
#pragma once
#include <cstdint>

namespace arcana {
namespace display {

using Color = uint16_t;  // RGB565

namespace colors {
    static const Color BLACK    = 0x0000;
    static const Color WHITE    = 0xFFFF;
    static const Color RED      = 0xF800;
    static const Color GREEN    = 0x07E0;
    static const Color BLUE     = 0x001F;
    static const Color YELLOW   = 0xFFE0;
    static const Color CYAN     = 0x07FF;
    static const Color MAGENTA  = 0xF81F;
    static const Color GRAY     = 0x7BEF;
    static const Color DARKGRAY = 0x39E7;
}

class IDisplay {
public:
    virtual ~IDisplay() {}

    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;

    virtual void fillScreen(Color color) = 0;
    virtual void fillRect(uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h, Color color) = 0;
    virtual void drawHLine(uint16_t x, uint16_t y,
                           uint16_t w, Color color) {
        fillRect(x, y, w, 1, color);
    }
    virtual void drawChar(uint16_t x, uint16_t y, char c,
                          Color fg, Color bg, uint8_t scale) = 0;
    virtual void drawString(uint16_t x, uint16_t y, const char* str,
                            Color fg, Color bg, uint8_t scale) = 0;

    /** Draw 1bpp XBM bitmap (same format as u8g2_DrawXBMP, LSB first) */
    virtual void drawXBitmap(uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const uint8_t* bitmap,
                             Color fg, Color bg) = 0;

    /** Draw RGB565 raw pixel buffer (default: slow per-pixel, override for DMA) */
    virtual void drawBitmap16(uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h,
                              const uint16_t* pixels) {
        for (uint16_t r = 0; r < h; r++)
            for (uint16_t c = 0; c < w; c++)
                fillRect(x + c, y + r, 1, 1, pixels[r * w + c]);
    }
};

/** Thread-safe global display for ad-hoc status callers */
extern IDisplay* g_display;

} // namespace display
} // namespace arcana
```

`g_display` **definition** (`IDisplay* g_display = nullptr;`) goes in `Controller.cpp` — no new .cpp needed.

### 2. Ili9341Display.hpp — `Services/Driver/Ili9341Display.hpp`

Header-only Adapter. Wraps existing `Ili9341Lcd` (unchanged). Pure delegation, zero overhead.

```cpp
#pragma once
#include "IDisplay.hpp"
#include "Ili9341Lcd.hpp"

namespace arcana {
namespace display {

class Ili9341Display : public IDisplay {
    lcd::Ili9341Lcd& mLcd;
public:
    explicit Ili9341Display(lcd::Ili9341Lcd& lcd) : mLcd(lcd) {}

    uint16_t width()  const override { return lcd::Ili9341Lcd::WIDTH; }
    uint16_t height() const override { return lcd::Ili9341Lcd::HEIGHT; }

    void fillScreen(Color c) override { mLcd.fillScreen(c); }
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, Color c) override {
        mLcd.fillRect(x, y, w, h, c);
    }
    void drawHLine(uint16_t x, uint16_t y, uint16_t w, Color c) override {
        mLcd.drawHLine(x, y, w, c);
    }
    void drawChar(uint16_t x, uint16_t y, char c, Color fg, Color bg, uint8_t s) override {
        mLcd.drawChar(x, y, c, fg, bg, s);
    }
    void drawString(uint16_t x, uint16_t y, const char* str, Color fg, Color bg, uint8_t s) override {
        mLcd.drawString(x, y, str, fg, bg, s);
    }
    void drawXBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint8_t* bitmap, Color fg, Color bg) override {
        mLcd.drawXBitmap(x, y, w, h, bitmap, fg, bg);  // FSMC-optimized
    }
};

} // namespace display
} // namespace arcana
```

### 2b. Ili9341Lcd — add drawXBitmap (new method on existing driver)

**Ili9341Lcd.hpp** — add declaration:
```cpp
void drawXBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 const uint8_t* bitmap, uint16_t fg, uint16_t bg);
```

**Ili9341Lcd.cpp** — add implementation (FSMC-optimized, setWindow + bulk write):
```cpp
void Ili9341Lcd::drawXBitmap(uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h,
                              const uint8_t* bitmap,
                              uint16_t fg, uint16_t bg) {
    if (x >= WIDTH || y >= HEIGHT) return;
    if (x + w > WIDTH) w = WIDTH - x;
    if (y + h > HEIGHT) h = HEIGHT - y;

    setWindow(x, y, x + w - 1, y + h - 1);
    writeCmd(0x2C);  // Memory Write

    uint16_t bytesPerRow = (w + 7) / 8;
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint8_t byte = bitmap[row * bytesPerRow + (col >> 3)];
            writeData((byte & (1 << (col & 7))) ? fg : bg);
        }
    }
}
```

### 3. MutexDisplay.hpp — `Shared/Inc/display/MutexDisplay.hpp`

Decorator: wraps any IDisplay with mutex for thread safety. Used by `g_display` so ad-hoc callers from different FreeRTOS tasks are safe.

```cpp
#pragma once
#include "IDisplay.hpp"
#include "IMutex.hpp"   // arcana::ats::IMutex

namespace arcana {
namespace display {

class MutexDisplay : public IDisplay {
    IDisplay& mInner;
    ats::IMutex& mMutex;
public:
    MutexDisplay(IDisplay& inner, ats::IMutex& mutex)
        : mInner(inner), mMutex(mutex) {}

    uint16_t width()  const override { return mInner.width(); }
    uint16_t height() const override { return mInner.height(); }

    void fillScreen(Color c) override {
        if (mMutex.lock(10)) { mInner.fillScreen(c); mMutex.unlock(); }
    }
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, Color c) override {
        if (mMutex.lock(10)) { mInner.fillRect(x, y, w, h, c); mMutex.unlock(); }
    }
    void drawHLine(uint16_t x, uint16_t y, uint16_t w, Color c) override {
        if (mMutex.lock(10)) { mInner.drawHLine(x, y, w, c); mMutex.unlock(); }
    }
    void drawChar(uint16_t x, uint16_t y, char c, Color fg, Color bg, uint8_t s) override {
        if (mMutex.lock(10)) { mInner.drawChar(x, y, c, fg, bg, s); mMutex.unlock(); }
    }
    void drawString(uint16_t x, uint16_t y, const char* str, Color fg, Color bg, uint8_t s) override {
        if (mMutex.lock(10)) { mInner.drawString(x, y, str, fg, bg, s); mMutex.unlock(); }
    }
    void drawXBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint8_t* bmp, Color fg, Color bg) override {
        if (mMutex.lock(10)) { mInner.drawXBitmap(x, y, w, h, bmp, fg, bg); mMutex.unlock(); }
    }
};

} // namespace display
} // namespace arcana
```

### 4. TouchTypes.hpp — `Shared/Inc/display/TouchTypes.hpp`

Data types for touch events and gestures. No driver dependency — pure data definitions. Future XPT2046 driver will produce these events.

```cpp
#pragma once
#include <cstdint>

namespace arcana {
namespace display {

/** Raw touch event from hardware driver */
struct TouchEvent {
    enum Type : uint8_t { Down, Move, Up };
    Type type;
    uint16_t x;   // display coordinates (after calibration)
    uint16_t y;
};

/** Processed gesture (from GestureDetector — future) */
enum class Gesture : uint8_t {
    Tap,           // short press + release
    LongPress,     // >500ms hold
    SwipeLeft,
    SwipeRight,
    SwipeUp,
    SwipeDown,
};

/** Touch point for gesture detector state (future use) */
struct TouchPoint {
    uint16_t x, y;
    uint32_t timestamp;  // tick count
};

/** Physical button event (KEY1/KEY2 on board, or encoder/D-pad) */
struct KeyEvent {
    enum Type : uint8_t { Press, Release, LongPress, Repeat };
    enum Key : uint8_t {
        Key1 = 0,   // PA0 — confirm / activate
        Key2 = 1,   // PC13 — navigate / back
        KeyUp,      // future: encoder CW or D-pad
        KeyDown,    // future: encoder CCW or D-pad
    };
    Type type;
    Key key;
};

} // namespace display
} // namespace arcana
```

### 5. Widget.hpp — `Shared/Inc/display/Widget.hpp`

Base class for all UI widgets. Common hit-test + virtual draw/handleTouch. `WidgetGroup` manages any mix of widget types on a View.

```cpp
#pragma once
#include "IDisplay.hpp"
#include "TouchTypes.hpp"

namespace arcana {
namespace display {

/**
 * Base UI widget — common rect + hit-test + virtual interface.
 * All form widgets (Button, Checkbox, Slider, etc.) derive from this.
 */
class Widget {
public:
    uint16_t x, y, w, h;           // position + size (also hit-test area)
    bool visible;
    bool enabled;
    bool focusable;                 // true = can receive key focus (光棒)

    Widget() : x(0), y(0), w(0), h(0), visible(true), enabled(true), focusable(true) {}
    Widget(uint16_t x_, uint16_t y_, uint16_t w_, uint16_t h_)
        : x(x_), y(y_), w(w_), h(h_), visible(true), enabled(true), focusable(true) {}
    virtual ~Widget() {}

    bool hitTest(uint16_t tx, uint16_t ty) const {
        return enabled && visible &&
               tx >= x && tx < (x + w) && ty >= y && ty < (y + h);
    }

    /** Draw widget */
    virtual void draw(IDisplay& display) = 0;

    /** Draw focus highlight border (光棒). Override for custom focus style. */
    virtual void drawFocus(IDisplay& display, bool focused) {
        Color c = focused ? colors::CYAN : colors::BLACK;
        display.drawHLine(x - 1, y - 1, w + 2, c);          // top
        display.drawHLine(x - 1, y + h, w + 2, c);          // bottom
        display.fillRect(x - 1, y, 1, h, c);                // left
        display.fillRect(x + w, y, 1, h, c);                // right
    }

    /** Handle touch event. Returns true if consumed. */
    virtual bool handleTouch(IDisplay& display, const TouchEvent& event, uint32_t tickCount) {
        (void)display; (void)event; (void)tickCount;
        return false;
    }

    /** Handle key event when focused. Returns true if consumed. */
    virtual bool onKey(IDisplay& display, const KeyEvent& event) {
        (void)display; (void)event;
        return false;
    }
};

/**
 * WidgetGroup — manages heterogeneous widgets on one view.
 * Call from View.onTouch() → routes to correct widget via hit-test.
 */
class WidgetGroup {
public:
    static const uint8_t MAX_WIDGETS = 12;

    WidgetGroup() : mCount(0), mPressedIndex(-1), mFocusIndex(-1) {
        for (uint8_t i = 0; i < MAX_WIDGETS; i++) mWidgets[i] = nullptr;
    }

    void add(Widget* w) {
        if (mCount < MAX_WIDGETS) mWidgets[mCount++] = w;
    }

    void drawAll(IDisplay& display) {
        for (uint8_t i = 0; i < mCount; i++) {
            if (mWidgets[i] && mWidgets[i]->visible) {
                mWidgets[i]->draw(display);
                if (i == mFocusIndex) mWidgets[i]->drawFocus(display, true);
            }
        }
    }

    // ── Touch handling ──

    bool handleTouch(IDisplay& display, const TouchEvent& event, uint32_t tickCount) {
        if (event.type == TouchEvent::Down) {
            for (uint8_t i = 0; i < mCount; i++) {
                if (mWidgets[i] && mWidgets[i]->hitTest(event.x, event.y)) {
                    // Touch sets focus to tapped widget
                    setFocus(display, (int8_t)i);
                    mPressedIndex = (int8_t)i;
                    return mWidgets[i]->handleTouch(display, event, tickCount);
                }
            }
            return false;
        }
        // Move/Up → forward to widget that received Down
        if (mPressedIndex >= 0) {
            bool consumed = mWidgets[mPressedIndex]->handleTouch(display, event, tickCount);
            if (event.type == TouchEvent::Up) mPressedIndex = -1;
            return consumed;
        }
        return false;
    }

    // ── Key / Focus handling (光棒) ──

    /** Move focus to next focusable widget (wraps around) */
    void focusNext(IDisplay& display) {
        int8_t start = mFocusIndex;
        int8_t idx = (start < 0) ? 0 : start + 1;
        for (uint8_t i = 0; i < mCount; i++) {
            if (idx >= mCount) idx = 0;
            if (mWidgets[idx] && mWidgets[idx]->focusable && mWidgets[idx]->enabled) {
                setFocus(display, idx);
                return;
            }
            idx++;
        }
    }

    /** Move focus to previous focusable widget */
    void focusPrev(IDisplay& display) {
        int8_t idx = (mFocusIndex <= 0) ? mCount - 1 : mFocusIndex - 1;
        for (uint8_t i = 0; i < mCount; i++) {
            if (idx < 0) idx = mCount - 1;
            if (mWidgets[idx] && mWidgets[idx]->focusable && mWidgets[idx]->enabled) {
                setFocus(display, idx);
                return;
            }
            idx--;
        }
    }

    /**
     * Handle physical key event.
     * Key2 short press → focusNext, Key2 long press → focusPrev.
     * Key1 → forward to focused widget's onKey().
     */
    bool handleKey(IDisplay& display, const KeyEvent& event) {
        if (event.type == KeyEvent::Press && event.key == KeyEvent::Key2) {
            focusNext(display);
            return true;
        }
        if (event.type == KeyEvent::LongPress && event.key == KeyEvent::Key2) {
            focusPrev(display);
            return true;
        }
        // Forward other keys to focused widget
        if (mFocusIndex >= 0 && mWidgets[mFocusIndex]) {
            return mWidgets[mFocusIndex]->onKey(display, event);
        }
        return false;
    }

    int8_t focusIndex() const { return mFocusIndex; }

private:
    void setFocus(IDisplay& display, int8_t newIndex) {
        if (mFocusIndex >= 0 && mFocusIndex < mCount && mWidgets[mFocusIndex])
            mWidgets[mFocusIndex]->drawFocus(display, false);  // clear old
        mFocusIndex = newIndex;
        if (mFocusIndex >= 0 && mFocusIndex < mCount && mWidgets[mFocusIndex])
            mWidgets[mFocusIndex]->drawFocus(display, true);   // draw new
    }

    Widget* mWidgets[MAX_WIDGETS];
    uint8_t mCount;
    int8_t mPressedIndex;
    int8_t mFocusIndex;         // current focus index (光棒), -1 = none
};

} // namespace display
} // namespace arcana
```

### 5b. BitmapButton.hpp — `Shared/Inc/display/BitmapButton.hpp`

XBM icon button with normal/pressed bitmaps + tap/longPress callbacks.

```cpp
#pragma once
#include "Widget.hpp"

namespace arcana {
namespace display {

class BitmapButton : public Widget {
public:
    const uint8_t* bitmap;              // XBM (normal)
    const uint8_t* bitmapPressed;       // XBM (pressed, null = no change)
    Color fg, bg;

    void (*onTap)(void* ctx);
    void (*onLongPress)(void* ctx);
    void* context;

    static const uint32_t LONG_PRESS_MS = 500;

    BitmapButton() : Widget(), bitmap(nullptr), bitmapPressed(nullptr),
        fg(colors::WHITE), bg(colors::BLACK),
        onTap(nullptr), onLongPress(nullptr), context(nullptr),
        mPressed(false), mDownTick(0) {}

    void setup(uint16_t x_, uint16_t y_, uint16_t w_, uint16_t h_,
               const uint8_t* bmp, const uint8_t* bmpPressed,
               Color fg_, Color bg_,
               void (*tap)(void*), void (*longPress)(void*), void* ctx) {
        x = x_; y = y_; w = w_; h = h_;
        bitmap = bmp; bitmapPressed = bmpPressed;
        fg = fg_; bg = bg_;
        onTap = tap; onLongPress = longPress; context = ctx;
    }

    void draw(IDisplay& display) override {
        const uint8_t* bmp = (mPressed && bitmapPressed) ? bitmapPressed : bitmap;
        if (bmp) display.drawXBitmap(x, y, w, h, bmp, fg, bg);
    }

    bool handleTouch(IDisplay& display, const TouchEvent& event, uint32_t tick) override {
        switch (event.type) {
        case TouchEvent::Down:
            mPressed = true; mDownTick = tick;
            draw(display);
            return true;
        case TouchEvent::Move:
            if (!hitTest(event.x, event.y) && mPressed) {
                mPressed = false; draw(display);
            }
            return mPressed;
        case TouchEvent::Up:
            if (mPressed) {
                mPressed = false; draw(display);
                uint32_t elapsed = tick - mDownTick;
                if (elapsed >= LONG_PRESS_MS) {
                    if (onLongPress) onLongPress(context);
                } else {
                    if (onTap) onTap(context);
                }
            }
            return true;
        }
        return false;
    }

    bool onKey(IDisplay& display, const KeyEvent& event) override {
        if (event.type == KeyEvent::Press && event.key == KeyEvent::Key1) {
            if (onTap) onTap(context);
            return true;
        }
        if (event.type == KeyEvent::LongPress && event.key == KeyEvent::Key1) {
            if (onLongPress) onLongPress(context);
            return true;
        }
        return false;
    }

private:
    bool mPressed;
    uint32_t mDownTick;
};

} // namespace display
} // namespace arcana
```

### 5c. FormWidgets.hpp — `Shared/Inc/display/FormWidgets.hpp`

HTML form element mapping for embedded LCD. All derive from `Widget`.

```cpp
#pragma once
#include "Widget.hpp"
#include <cstdio>

namespace arcana {
namespace display {

// ─────────────────────────────────────────────────
// Label  (<label>) — read-only text display
// ─────────────────────────────────────────────────
class Label : public Widget {
public:
    const char* text;
    Color fg, bg;
    uint8_t scale;

    Label() : Widget(), text(""), fg(colors::WHITE), bg(colors::BLACK), scale(1) {
        focusable = false;  // read-only, skip in focus navigation
    }

    void setup(uint16_t x_, uint16_t y_, const char* txt,
               Color fg_ = colors::WHITE, Color bg_ = colors::BLACK, uint8_t s = 1) {
        x = x_; y = y_; text = txt; fg = fg_; bg = bg_; scale = s;
        w = 0; h = scale * 7;  // width determined by text length
    }

    void draw(IDisplay& display) override {
        display.drawString(x, y, text, fg, bg, scale);
    }

    /** Update text and redraw with clear */
    void setText(IDisplay& display, const char* newText, uint16_t clearW = 0) {
        if (clearW > 0) display.fillRect(x, y, clearW, scale * 7, bg);
        text = newText;
        draw(display);
    }
};

// ─────────────────────────────────────────────────
// Checkbox  (<input type="checkbox">) — toggle true/false
// ─────────────────────────────────────────────────
class Checkbox : public Widget {
public:
    bool checked;
    Color fg, bg, checkColor;
    const char* label;              // optional text after box

    void (*onChange)(bool checked, void* ctx);
    void* context;

    static const uint16_t BOX_SIZE = 12;

    Checkbox() : Widget(0,0,BOX_SIZE,BOX_SIZE), checked(false),
        fg(colors::WHITE), bg(colors::BLACK), checkColor(colors::GREEN),
        label(nullptr), onChange(nullptr), context(nullptr) {}

    void setup(uint16_t x_, uint16_t y_, const char* lbl,
               void (*cb)(bool, void*), void* ctx,
               bool initial = false) {
        x = x_; y = y_; w = BOX_SIZE; h = BOX_SIZE;
        label = lbl; onChange = cb; context = ctx; checked = initial;
        if (label) w += 6 + 6 * 1;  // approx label width (not used for hit-test, only box)
    }

    void draw(IDisplay& display) override {
        // Outer box
        display.fillRect(x, y, BOX_SIZE, BOX_SIZE, bg);
        display.drawHLine(x, y, BOX_SIZE, fg);
        display.drawHLine(x, y + BOX_SIZE - 1, BOX_SIZE, fg);
        display.fillRect(x, y, 1, BOX_SIZE, fg);
        display.fillRect(x + BOX_SIZE - 1, y, 1, BOX_SIZE, fg);
        // Check mark (filled inner square)
        if (checked) {
            display.fillRect(x + 3, y + 3, BOX_SIZE - 6, BOX_SIZE - 6, checkColor);
        }
        // Label
        if (label) {
            display.drawString(x + BOX_SIZE + 4, y + 2, label, fg, bg, 1);
        }
    }

    bool handleTouch(IDisplay& display, const TouchEvent& event, uint32_t tick) override {
        (void)tick;
        if (event.type == TouchEvent::Up && hitTest(event.x, event.y)) {
            toggle(display);
            return true;
        }
        return event.type == TouchEvent::Down;
    }

    bool onKey(IDisplay& display, const KeyEvent& event) override {
        if (event.type == KeyEvent::Press && event.key == KeyEvent::Key1) {
            toggle(display);
            return true;
        }
        return false;
    }

private:
    void toggle(IDisplay& display) {
        checked = !checked;
        draw(display);
        if (onChange) onChange(checked, context);
    }
};

// ─────────────────────────────────────────────────
// RadioGroup  (<input type="radio">) — select one from N
// ─────────────────────────────────────────────────
class RadioGroup : public Widget {
public:
    static const uint8_t MAX_OPTIONS = 6;
    static const uint16_t OPTION_H = 16;
    static const uint16_t DOT_SIZE = 10;

    const char* options[MAX_OPTIONS];
    uint8_t optionCount;
    uint8_t selected;
    Color fg, bg, selectColor;

    void (*onChange)(uint8_t index, void* ctx);
    void* context;

    RadioGroup() : Widget(), optionCount(0), selected(0),
        fg(colors::WHITE), bg(colors::BLACK), selectColor(colors::CYAN),
        onChange(nullptr), context(nullptr) {}

    void setup(uint16_t x_, uint16_t y_, const char** opts, uint8_t count,
               void (*cb)(uint8_t, void*), void* ctx, uint8_t initial = 0) {
        x = x_; y = y_;
        w = 120; h = count * OPTION_H;
        optionCount = (count > MAX_OPTIONS) ? MAX_OPTIONS : count;
        for (uint8_t i = 0; i < optionCount; i++) options[i] = opts[i];
        onChange = cb; context = ctx; selected = initial;
    }

    void draw(IDisplay& display) override {
        for (uint8_t i = 0; i < optionCount; i++) {
            uint16_t oy = y + i * OPTION_H;
            // Circle outline (approx with rect for simplicity)
            display.fillRect(x, oy + 2, DOT_SIZE, DOT_SIZE, bg);
            display.drawHLine(x + 2, oy + 2, DOT_SIZE - 4, fg);
            display.drawHLine(x + 2, oy + DOT_SIZE + 1, DOT_SIZE - 4, fg);
            display.fillRect(x, oy + 4, 1, DOT_SIZE - 4, fg);
            display.fillRect(x + DOT_SIZE - 1, oy + 4, 1, DOT_SIZE - 4, fg);
            // Filled dot if selected
            if (i == selected) {
                display.fillRect(x + 3, oy + 5, DOT_SIZE - 6, DOT_SIZE - 6, selectColor);
            }
            // Label
            if (options[i]) {
                display.drawString(x + DOT_SIZE + 4, oy + 3, options[i], fg, bg, 1);
            }
        }
    }

    bool handleTouch(IDisplay& display, const TouchEvent& event, uint32_t tick) override {
        (void)tick;
        if (event.type == TouchEvent::Up) {
            uint8_t idx = (event.y - y) / OPTION_H;
            if (idx < optionCount && idx != selected) {
                selected = idx;
                draw(display);
                if (onChange) onChange(selected, context);
                return true;
            }
        }
        return event.type == TouchEvent::Down;
    }
};

// ─────────────────────────────────────────────────
// Slider  (<input type="range">) — horizontal drag
// ─────────────────────────────────────────────────
class Slider : public Widget {
public:
    int16_t value;
    int16_t minVal, maxVal;
    Color trackColor, thumbColor, bg;
    static const uint16_t THUMB_W = 8;
    static const uint16_t TRACK_H = 4;

    void (*onChange)(int16_t value, void* ctx);
    void* context;

    Slider() : Widget(), value(0), minVal(0), maxVal(100),
        trackColor(colors::GRAY), thumbColor(colors::CYAN), bg(colors::BLACK),
        onChange(nullptr), context(nullptr) {}

    void setup(uint16_t x_, uint16_t y_, uint16_t w_,
               int16_t min_, int16_t max_, int16_t initial,
               void (*cb)(int16_t, void*), void* ctx) {
        x = x_; y = y_; w = w_; h = 16;
        minVal = min_; maxVal = max_; value = initial;
        onChange = cb; context = ctx;
    }

    void draw(IDisplay& display) override {
        // Background
        display.fillRect(x, y, w, h, bg);
        // Track
        uint16_t trackY = y + (h - TRACK_H) / 2;
        display.fillRect(x, trackY, w, TRACK_H, trackColor);
        // Thumb
        uint16_t thumbX = valueToX();
        display.fillRect(thumbX, y, THUMB_W, h, thumbColor);
    }

    bool handleTouch(IDisplay& display, const TouchEvent& event, uint32_t tick) override {
        (void)tick;
        if (event.type == TouchEvent::Down || event.type == TouchEvent::Move) {
            int16_t newVal = xToValue(event.x);
            if (newVal != value) {
                value = newVal;
                draw(display);
                if (onChange) onChange(value, context);
            }
            return true;
        }
        return false;
    }

    bool onKey(IDisplay& display, const KeyEvent& event) override {
        if (event.type == KeyEvent::Press && event.key == KeyEvent::Key1) {
            // Key1: increment by ~5% of range
            int16_t step = (maxVal - minVal) / 20;
            if (step < 1) step = 1;
            if (value + step <= maxVal) value += step; else value = maxVal;
            draw(display);
            if (onChange) onChange(value, context);
            return true;
        }
        return false;
    }

private:
    uint16_t valueToX() const {
        if (maxVal == minVal) return x;
        return x + (uint32_t)(value - minVal) * (w - THUMB_W) / (maxVal - minVal);
    }
    int16_t xToValue(uint16_t tx) const {
        if (tx <= x + THUMB_W / 2) return minVal;
        if (tx >= x + w - THUMB_W / 2) return maxVal;
        return minVal + (int32_t)(tx - x - THUMB_W / 2) * (maxVal - minVal) / (w - THUMB_W);
    }
};

// ─────────────────────────────────────────────────
// ProgressBar  (<progress>) — read-only fill bar
// ─────────────────────────────────────────────────
class ProgressBar : public Widget {
public:
    uint8_t percent;    // 0-100
    Color fillColor, trackColor, borderColor, bg;

    ProgressBar() : Widget(), percent(0),
        fillColor(colors::GREEN), trackColor(colors::DARKGRAY),
        borderColor(colors::WHITE), bg(colors::BLACK) {
        focusable = false;  // read-only, skip in focus navigation
    }

    void setup(uint16_t x_, uint16_t y_, uint16_t w_, uint16_t h_) {
        x = x_; y = y_; w = w_; h = h_;
    }

    void setPercent(IDisplay& display, uint8_t pct) {
        percent = (pct > 100) ? 100 : pct;
        draw(display);
    }

    void draw(IDisplay& display) override {
        // Border
        display.drawHLine(x, y, w, borderColor);
        display.drawHLine(x, y + h - 1, w, borderColor);
        display.fillRect(x, y, 1, h, borderColor);
        display.fillRect(x + w - 1, y, 1, h, borderColor);
        // Fill
        uint16_t fillW = (uint32_t)(w - 2) * percent / 100;
        if (fillW > 0)
            display.fillRect(x + 1, y + 1, fillW, h - 2, fillColor);
        // Empty
        uint16_t emptyW = (w - 2) - fillW;
        if (emptyW > 0)
            display.fillRect(x + 1 + fillW, y + 1, emptyW, h - 2, trackColor);
    }
};

// ─────────────────────────────────────────────────
// NumberStepper  (<input type="number">) — [-] value [+]
// ─────────────────────────────────────────────────
class NumberStepper : public Widget {
public:
    int16_t value;
    int16_t minVal, maxVal, step;
    Color fg, bg, btnColor;
    static const uint16_t BTN_W = 20;
    static const uint16_t VAL_W = 40;

    void (*onChange)(int16_t value, void* ctx);
    void* context;

    NumberStepper() : Widget(), value(0), minVal(0), maxVal(100), step(1),
        fg(colors::WHITE), bg(colors::BLACK), btnColor(colors::DARKGRAY),
        onChange(nullptr), context(nullptr) {}

    void setup(uint16_t x_, uint16_t y_,
               int16_t min_, int16_t max_, int16_t initial, int16_t step_,
               void (*cb)(int16_t, void*), void* ctx) {
        x = x_; y = y_;
        w = BTN_W + VAL_W + BTN_W; h = 16;
        minVal = min_; maxVal = max_; value = initial; step = step_;
        onChange = cb; context = ctx;
    }

    void draw(IDisplay& display) override {
        // [-] button
        display.fillRect(x, y, BTN_W, h, btnColor);
        display.drawString(x + 6, y + 4, "-", fg, btnColor, 1);
        // Value
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", (int)value);
        display.fillRect(x + BTN_W, y, VAL_W, h, bg);
        display.drawString(x + BTN_W + 8, y + 4, buf, fg, bg, 1);
        // [+] button
        display.fillRect(x + BTN_W + VAL_W, y, BTN_W, h, btnColor);
        display.drawString(x + BTN_W + VAL_W + 6, y + 4, "+", fg, btnColor, 1);
    }

    bool handleTouch(IDisplay& display, const TouchEvent& event, uint32_t tick) override {
        (void)tick;
        if (event.type != TouchEvent::Up) return event.type == TouchEvent::Down;
        if (event.x < x + BTN_W) {
            decrement(display);
        } else if (event.x >= x + BTN_W + VAL_W) {
            increment(display);
        } else {
            return false;
        }
        return true;
    }

    bool onKey(IDisplay& display, const KeyEvent& event) override {
        if (event.type == KeyEvent::Press && event.key == KeyEvent::Key1) {
            increment(display);
            return true;
        }
        if (event.type == KeyEvent::LongPress && event.key == KeyEvent::Key1) {
            decrement(display);
            return true;
        }
        return false;
    }

private:
    void increment(IDisplay& display) {
        value = (value + step <= maxVal) ? value + step : maxVal;
        draw(display);
        if (onChange) onChange(value, context);
    }
    void decrement(IDisplay& display) {
        value = (value - step >= minVal) ? value - step : minVal;
        draw(display);
        if (onChange) onChange(value, context);
    }
};

} // namespace display
} // namespace arcana
```

**HTML form element → Embedded widget mapping:**

| HTML / Mobile Element | Widget Class | Interactive | Input |
|---|---|---|---|
| `<button>` | `BitmapButton` | Yes | Tap / LongPress / Key1 |
| `<input type="checkbox">` | `Checkbox` | Yes | Tap / Key1 toggles |
| `<input type="radio">` | `RadioGroup` | Yes | Tap / Key1 selects |
| `<input type="range">` | `Slider` | Yes | Drag / Key1 adjusts |
| `<progress>` | `ProgressBar` | No | Read-only (not focusable) |
| `<label>` | `Label` | No | Read-only (not focusable) |
| `<input type="number">` | `NumberStepper` | Yes | Tap [-][+] / Key1 |
| `alert()` | `AlertDialog` | Yes | Modal: OK button / Key1 |
| `confirm()` | `ConfirmDialog` | Yes | Modal: OK/Cancel / Key1+Key2 |
| Android Toast | `Toast` | No | Auto-dismiss overlay |
| Status bar | `statusLine()` | No | Bottom bar (y=154) |
| Header bar | `headerBar()` | No | Top bar (y=0) |

### 6. DialogWidgets.hpp — `Shared/Inc/display/DialogWidgets.hpp`

Modal overlay dialogs — AlertDialog (OK only), ConfirmDialog (OK/Cancel), Toast (auto-dismiss).
Dialogs draw on top of everything, block touch to underlying view until dismissed.

```cpp
#pragma once
#include "IDisplay.hpp"
#include "TouchTypes.hpp"

namespace arcana {
namespace display {

/**
 * AlertDialog — modal popup with title, message, [OK] button.
 * Draws centered overlay box. Dismisses on OK tap or Key1 press.
 */
class AlertDialog {
public:
    static const uint16_t DLG_W = 200;
    static const uint16_t DLG_H = 80;
    static const uint16_t BTN_W = 60;
    static const uint16_t BTN_H = 20;

    const char* title;
    const char* message;
    void (*onDismiss)(void* ctx);
    void* context;
    bool active;

    AlertDialog() : title("Alert"), message(""), onDismiss(nullptr),
        context(nullptr), active(false) {}

    void show(IDisplay& display, const char* t, const char* msg,
              void (*cb)(void*) = nullptr, void* ctx = nullptr) {
        title = t; message = msg; onDismiss = cb; context = ctx;
        active = true;
        draw(display);
    }

    void draw(IDisplay& display) {
        uint16_t x = (display.width() - DLG_W) / 2;
        uint16_t y = (display.height() - DLG_H) / 2;
        // Background + border
        display.fillRect(x, y, DLG_W, DLG_H, colors::BLACK);
        display.drawHLine(x, y, DLG_W, colors::WHITE);
        display.drawHLine(x, y + DLG_H - 1, DLG_W, colors::WHITE);
        display.fillRect(x, y, 1, DLG_H, colors::WHITE);
        display.fillRect(x + DLG_W - 1, y, 1, DLG_H, colors::WHITE);
        // Title
        display.drawString(x + 8, y + 6, title, colors::YELLOW, colors::BLACK, 1);
        display.drawHLine(x + 4, y + 16, DLG_W - 8, colors::DARKGRAY);
        // Message
        display.drawString(x + 8, y + 22, message, colors::WHITE, colors::BLACK, 1);
        // [OK] button
        mBtnX = x + (DLG_W - BTN_W) / 2;
        mBtnY = y + DLG_H - BTN_H - 6;
        display.fillRect(mBtnX, mBtnY, BTN_W, BTN_H, colors::DARKGRAY);
        display.drawString(mBtnX + 20, mBtnY + 6, "OK", colors::WHITE, colors::DARKGRAY, 1);
    }

    bool handleTouch(IDisplay& display, const TouchEvent& event) {
        if (!active) return false;
        if (event.type == TouchEvent::Up) {
            if (event.x >= mBtnX && event.x < mBtnX + BTN_W &&
                event.y >= mBtnY && event.y < mBtnY + BTN_H) {
                dismiss(display);
            }
        }
        return true;  // consume all touch while dialog active
    }

    bool handleKey(IDisplay& display, const KeyEvent& event) {
        if (!active) return false;
        if (event.type == KeyEvent::Press && event.key == KeyEvent::Key1) {
            dismiss(display);
        }
        return true;  // consume all keys while dialog active
    }

private:
    uint16_t mBtnX, mBtnY;
    void dismiss(IDisplay& display) {
        active = false;
        if (onDismiss) onDismiss(context);
        // Caller must redraw underlying view
    }
};

/**
 * ConfirmDialog — modal with title, message, [OK] + [Cancel].
 * Callback receives result (true=OK, false=Cancel).
 */
class ConfirmDialog {
public:
    static const uint16_t DLG_W = 200;
    static const uint16_t DLG_H = 80;
    static const uint16_t BTN_W = 50;
    static const uint16_t BTN_H = 20;

    const char* title;
    const char* message;
    void (*onResult)(bool ok, void* ctx);
    void* context;
    bool active;
    uint8_t focusBtn;  // 0=Cancel, 1=OK (for key navigation)

    ConfirmDialog() : title("Confirm"), message(""), onResult(nullptr),
        context(nullptr), active(false), focusBtn(1) {}

    void show(IDisplay& display, const char* t, const char* msg,
              void (*cb)(bool, void*), void* ctx = nullptr) {
        title = t; message = msg; onResult = cb; context = ctx;
        active = true; focusBtn = 1;
        draw(display);
    }

    void draw(IDisplay& display) {
        uint16_t x = (display.width() - DLG_W) / 2;
        uint16_t y = (display.height() - DLG_H) / 2;
        // Background + border
        display.fillRect(x, y, DLG_W, DLG_H, colors::BLACK);
        display.drawHLine(x, y, DLG_W, colors::WHITE);
        display.drawHLine(x, y + DLG_H - 1, DLG_W, colors::WHITE);
        display.fillRect(x, y, 1, DLG_H, colors::WHITE);
        display.fillRect(x + DLG_W - 1, y, 1, DLG_H, colors::WHITE);
        // Title + message
        display.drawString(x + 8, y + 6, title, colors::YELLOW, colors::BLACK, 1);
        display.drawHLine(x + 4, y + 16, DLG_W - 8, colors::DARKGRAY);
        display.drawString(x + 8, y + 22, message, colors::WHITE, colors::BLACK, 1);
        // [Cancel] button
        mCancelX = x + DLG_W / 2 - BTN_W - 10;
        mBtnY = y + DLG_H - BTN_H - 6;
        Color cancelBg = (focusBtn == 0) ? colors::CYAN : colors::DARKGRAY;
        display.fillRect(mCancelX, mBtnY, BTN_W, BTN_H, cancelBg);
        display.drawString(mCancelX + 4, mBtnY + 6, "Cancel", colors::WHITE, cancelBg, 1);
        // [OK] button
        mOkX = x + DLG_W / 2 + 10;
        Color okBg = (focusBtn == 1) ? colors::CYAN : colors::DARKGRAY;
        display.fillRect(mOkX, mBtnY, BTN_W, BTN_H, okBg);
        display.drawString(mOkX + 16, mBtnY + 6, "OK", colors::WHITE, okBg, 1);
    }

    bool handleTouch(IDisplay& display, const TouchEvent& event) {
        if (!active) return false;
        if (event.type == TouchEvent::Up && event.y >= mBtnY && event.y < mBtnY + BTN_H) {
            if (event.x >= mCancelX && event.x < mCancelX + BTN_W) {
                active = false;
                if (onResult) onResult(false, context);
            } else if (event.x >= mOkX && event.x < mOkX + BTN_W) {
                active = false;
                if (onResult) onResult(true, context);
            }
        }
        return true;  // consume all touch while active
    }

    bool handleKey(IDisplay& display, const KeyEvent& event) {
        if (!active) return false;
        if (event.type == KeyEvent::Press) {
            if (event.key == KeyEvent::Key2) {
                focusBtn = 1 - focusBtn;  // toggle OK/Cancel focus
                draw(display);
            } else if (event.key == KeyEvent::Key1) {
                active = false;
                if (onResult) onResult(focusBtn == 1, context);
            }
        }
        return true;
    }

private:
    uint16_t mCancelX, mOkX, mBtnY;
};

/**
 * Toast — auto-dismiss overlay message (like Android Toast).
 * Shows for N ticks then disappears. Non-modal (doesn't block input).
 */
class Toast {
public:
    bool active;
    uint32_t dismissTick;   // tick when toast should disappear

    Toast() : active(false), dismissTick(0) {}

    void show(IDisplay& display, const char* msg, uint32_t durationMs,
              uint32_t currentTick, Color color = colors::WHITE) {
        active = true;
        dismissTick = currentTick + durationMs;
        // Draw centered at bottom
        uint16_t msgW = 0;
        const char* p = msg; while (*p++) msgW += 6;  // approx 6px per char
        uint16_t x = (display.width() - msgW) / 2;
        uint16_t y = display.height() - 24;
        mX = x - 4; mY = y - 4; mW = msgW + 8; mH = 16;
        display.fillRect(mX, mY, mW, mH, colors::DARKGRAY);
        display.drawString(x, y, msg, color, colors::DARKGRAY, 1);
    }

    /** Call periodically (e.g., from render loop). Returns true if toast just expired. */
    bool update(IDisplay& display, uint32_t currentTick) {
        if (active && currentTick >= dismissTick) {
            active = false;
            display.fillRect(mX, mY, mW, mH, colors::BLACK);
            return true;  // expired — caller should redraw area
        }
        return false;
    }

private:
    uint16_t mX, mY, mW, mH;  // toast rect (for clearing)
};

} // namespace display
} // namespace arcana
```

### 7. DisplayStatus.hpp — `Shared/Inc/display/DisplayStatus.hpp`

Unified overlay utilities — status line (bottom), header bar (top), toast helper.

```cpp
#pragma once
#include "IDisplay.hpp"

namespace arcana {
namespace display {

// ── Layout constants (240x320 portrait) ──
static const uint16_t HEADER_Y = 0;
static const uint16_t HEADER_H = 12;
static const uint16_t STATUS_Y = 154;
static const uint16_t STATUS_H = 8;
static const uint16_t STATUS_X = 20;

/** Header bar — top of screen (title / connection status icons) */
inline void headerBar(const char* title, Color fg = colors::WHITE, Color bg = colors::DARKGRAY) {
    if (!g_display) return;
    g_display->fillRect(0, HEADER_Y, 240, HEADER_H, bg);
    g_display->drawString(4, HEADER_Y + 2, title, fg, bg, 1);
}

/** Status line — below MQTT, above ECG (consistent across services) */
inline void statusLine(const char* msg, Color color = colors::WHITE) {
    if (!g_display) return;
    g_display->fillRect(0, STATUS_Y, 240, STATUS_H, colors::BLACK);
    g_display->drawString(STATUS_X, STATUS_Y, msg, color, colors::BLACK, 1);
}

inline void clearStatusLine() {
    if (!g_display) return;
    g_display->fillRect(0, STATUS_Y, 240, STATUS_H, colors::BLACK);
}

} // namespace display
} // namespace arcana
```

### 7. ViewManager.hpp — `Services/View/ViewManager.hpp`

**Stack-based navigation** — root-level swipe switching + push/pop sub-views.

```
Navigation model:
  Root level (swipe):  MainView ↔ EcgView ↔ SettingsView
  Sub level (push/pop): SettingsView → WiFiSettings → WiFiDetail
                                     → TimeSettings
                                     → DisplaySettings

  SwipeRight at depth>1 = pop (back)
  KEY2 long at depth>1 = pop (back)
```

```cpp
#pragma once
#include "IDisplay.hpp"
#include "TouchTypes.hpp"
#include "LcdView.hpp"
#include "LcdViewModel.hpp"

namespace arcana {
namespace lcd {

class ViewManager {
public:
    static const uint8_t MAX_ROOT_VIEWS = 4;
    static const uint8_t MAX_STACK_DEPTH = 4;

    ViewManager()
        : mDisplay(0), mRootCount(0), mRootIndex(0), mStackDepth(0) {}

    void init(display::IDisplay* display) { mDisplay = display; }

    /** Add a root-level view (MainView, EcgView, SettingsView) */
    void addRootView(LcdView* view) {
        if (mRootCount < MAX_ROOT_VIEWS) mRootViews[mRootCount++] = view;
    }

    // ── Root-level switching (swipe left/right) ──

    void switchRoot(uint8_t index) {
        if (index >= mRootCount || !mDisplay) return;
        // Exit entire stack
        if (mStackDepth > 0) currentView()->onExit(*mDisplay);
        mRootIndex = index;
        mStack[0] = mRootViews[index];
        mStackDepth = 1;
        mStack[0]->onEnter(*mDisplay);
    }

    void nextRoot() {
        if (mRootIndex + 1 < mRootCount) switchRoot(mRootIndex + 1);
    }
    void prevRoot() {
        if (mRootIndex > 0) switchRoot(mRootIndex - 1);
    }

    // ── Push/Pop navigation (sub-views) ──

    /** Push sub-view onto stack (e.g., SettingsView → WiFiSettings) */
    void push(LcdView* view) {
        if (!mDisplay || mStackDepth >= MAX_STACK_DEPTH) return;
        currentView()->onExit(*mDisplay);
        mStack[mStackDepth++] = view;
        view->onEnter(*mDisplay);
    }

    /** Pop back to previous view. Returns false if already at root. */
    bool pop() {
        if (!mDisplay || mStackDepth <= 1) return false;
        currentView()->onExit(*mDisplay);
        mStackDepth--;
        currentView()->onEnter(*mDisplay);
        return true;
    }

    /** Pop all sub-views, return to root */
    void popToRoot() {
        if (!mDisplay || mStackDepth <= 1) return;
        currentView()->onExit(*mDisplay);
        mStackDepth = 1;
        currentView()->onEnter(*mDisplay);
    }

    // ── Input routing ──

    bool dispatchTouch(const display::TouchEvent& event) {
        return currentView() ? currentView()->onTouch(event) : false;
    }

    bool dispatchGesture(display::Gesture gesture) {
        using G = display::Gesture;

        if (gesture == G::SwipeRight) {
            if (mStackDepth > 1) { pop(); return true; }     // sub: go back
            if (mRootIndex > 0) { prevRoot(); return true; }  // root: prev
        }
        if (gesture == G::SwipeLeft) {
            if (mStackDepth == 1 && mRootIndex + 1 < mRootCount) {
                nextRoot(); return true;                       // root: next
            }
        }
        // Forward other gestures to current view
        return currentView() ? currentView()->onGesture(gesture) : false;
    }

    bool dispatchKey(const display::KeyEvent& event) {
        using K = display::KeyEvent;
        // KEY2 long press at sub-level = back
        if (event.type == K::LongPress && event.key == K::Key2 && mStackDepth > 1) {
            pop();
            return true;
        }
        // Forward to current view
        return currentView() ? currentView()->onKey(event) : false;
    }

    // ── Accessors ──

    LcdView* currentView() const {
        return (mStackDepth > 0) ? mStack[mStackDepth - 1] : nullptr;
    }
    uint8_t depth() const { return mStackDepth; }
    uint8_t rootIndex() const { return mRootIndex; }
    bool isAtRoot() const { return mStackDepth <= 1; }

private:
    display::IDisplay* mDisplay;

    // Root level (swipe)
    LcdView* mRootViews[MAX_ROOT_VIEWS];
    uint8_t mRootCount;
    uint8_t mRootIndex;

    // Navigation stack
    LcdView* mStack[MAX_STACK_DEPTH];
    uint8_t mStackDepth;              // 0=none, 1=root, 2+=sub
};

} // namespace lcd
} // namespace arcana
```

**Controller wiring example:**
```cpp
// Root views
viewManager.addRootView(&mainView);       // [0] dashboard
viewManager.addRootView(&ecgView);        // [1] full-screen ECG
viewManager.addRootView(&settingsView);   // [2] settings menu
viewManager.switchRoot(0);

// SettingsView pushes sub-views on tap:
// onTap "WiFi" → viewManager.push(&wifiSettingsView);
// onTap "Time" → viewManager.push(&timeSettingsView);
```

### 8. LcdView.hpp Changes

```cpp
// Before:
#include "Ili9341Lcd.hpp"
// After:
#include "IDisplay.hpp"
#include "TouchTypes.hpp"

namespace arcana { namespace lcd { class ViewManager; } }  // forward

class LcdView {
public:
    virtual ~LcdView() {}

    // Display output (Ili9341Lcd& → display::IDisplay&)
    virtual void onEnter(display::IDisplay& display) = 0;
    virtual void onExit(display::IDisplay& display) { (void)display; }
    virtual void render(display::IDisplay& display, const LcdOutput& output, LcdOutput& rendered) = 0;
    virtual void renderEcgColumn(display::IDisplay& display, uint8_t x, uint8_t y, uint8_t prevY) { ... }

    // Touch input
    virtual bool onTouch(const display::TouchEvent& event) { (void)event; return false; }
    virtual bool onGesture(display::Gesture gesture) { (void)gesture; return false; }

    // Physical key input (KEY1/KEY2)
    virtual bool onKey(const display::KeyEvent& event) { (void)event; return false; }

    // Navigation — set by ViewManager, views use this to push sub-views
    ViewManager* navigator;  // set by ViewManager when view becomes active
};
```

Sub-views use `navigator` to push/pop:
```cpp
void SettingsView::onWifiTap(void* ctx) {
    auto* self = static_cast<SettingsView*>(ctx);
    if (self->navigator) self->navigator->push(&sWifiSettingsView);
}
```

### 8. MainView.hpp Changes

```cpp
// Input struct:
struct Input {
    LcdViewModel* viewModel;
    display::IDisplay* lcd;  // was Ili9341Lcd*
};

// All method signatures: Ili9341Lcd& → display::IDisplay&
void onEnter(display::IDisplay& lcd) override;
void render(display::IDisplay& lcd, const LcdOutput& output, LcdOutput& rendered) override;
void renderEcgColumn(display::IDisplay& lcd, uint8_t x, uint8_t y, uint8_t prevY) override;

// Private helpers: same change
void renderTemp(display::IDisplay& lcd, ...);
void renderSdInfo(display::IDisplay& lcd, ...);
void renderStorage(display::IDisplay& lcd, ...);
void renderTime(display::IDisplay& lcd, ...);
```

### 9. MainView.cpp Color Constant Changes

~25 replacements: `Ili9341Lcd::BLACK` → `display::colors::BLACK`, etc.

Full list of constants used:
- `Ili9341Lcd::BLACK` → `display::colors::BLACK`
- `Ili9341Lcd::WHITE` → `display::colors::WHITE`
- `Ili9341Lcd::YELLOW` → `display::colors::YELLOW`
- `Ili9341Lcd::GREEN` → `display::colors::GREEN`
- `Ili9341Lcd::CYAN` → `display::colors::CYAN`
- `Ili9341Lcd::GRAY` → `display::colors::GRAY`
- `Ili9341Lcd::DARKGRAY` → `display::colors::DARKGRAY`

Remove `#include "Ili9341Lcd.hpp"` (no longer needed — LcdView.hpp brings in IDisplay.hpp).

### 10. EcgBuffer.hpp + EcgDisplay.cpp Changes

```cpp
// EcgBuffer.hpp: replace forward decl with include
#include "IDisplay.hpp"

// Change member:
display::IDisplay* lcd;  // was lcd::Ili9341Lcd*

// Change init:
void init(display::IDisplay* lcdPtr) { ... }

// EcgDisplay.cpp: remove #include "Ili9341Lcd.hpp"
// Change color refs: lcd::Ili9341Lcd::BLACK → display::colors::BLACK
//                    lcd::Ili9341Lcd::GREEN → display::colors::GREEN
```

### 11. LcdService.hpp + LcdServiceImpl Changes

```cpp
// LcdService.hpp:
#include "IDisplay.hpp"  // was Ili9341Lcd.hpp
virtual display::IDisplay& getDisplay() = 0;  // was getLcd()

// LcdServiceImpl.hpp:
#include "Ili9341Display.hpp"
class LcdServiceImpl : public LcdService {
    // Add:
    display::Ili9341Display mAdapter;
    // Change:
    display::IDisplay& getDisplay() override { return mAdapter; }
private:
    lcd::Ili9341Lcd mLcd;  // kept (hardware driver unchanged)
};

// LcdServiceImpl.cpp:
// Constructor init list: mAdapter(mLcd)
// initHAL unchanged (still calls mLcd.initHAL())
```

**Important**: `mAdapter` must be declared AFTER `mLcd` so the reference is valid at construction.

### 12. Controller.cpp Changes

```cpp
// Add includes:
#include "IDisplay.hpp"
#include "MutexDisplay.hpp"
#include "FreeRtosMutex.hpp"

// Add g_display definition + decorator chain:
namespace arcana { namespace display { IDisplay* g_display = nullptr; } }

static ats::FreeRtosMutex sDispMutex;

// In wireViews():
sMainView.input.lcd = &mLcd->getDisplay();  // was &mLcd->getLcd()

// In initHAL(), after mLcd->initHAL():
sDispMutex.init();
display::g_display = /* MutexDisplay wrapping the adapter */;
```

For the MutexDisplay wiring, we need a static instance:
```cpp
static display::MutexDisplay* sMutexDisp = nullptr;

// In initHAL():
static display::MutexDisplay sMutexDispObj(mLcd->getDisplay(), sDispMutex);
sMutexDisp = &sMutexDispObj;
display::g_display = sMutexDisp;
```

Note: MainView already has its own `mLcdMutex` for render task synchronization, so it uses the raw adapter (not MutexDisplay). `g_display` with MutexDisplay is for ad-hoc cross-task callers only.

### 13. Ad-hoc Service Changes

All three services get the same treatment:

**AtsStorageServiceImpl.cpp** (line 12, 77-82):
- Remove `#include "Ili9341Lcd.hpp"`
- Add `#include "DisplayStatus.hpp"`
- Remove local `lcdStatus()` function
- Replace all `lcdStatus("msg", color)` → `display::statusLine("msg", color)`
- Replace `lcdStatus("msg")` → `display::statusLine("msg")`

**SdBenchmarkServiceImpl.cpp** (line 4, 9-13):
- Remove `#include "Ili9341Lcd.hpp"`
- Add `#include "DisplayStatus.hpp"`
- Remove local `lcdMsg()` function
- Replace all `lcdMsg("msg", color)` → `display::statusLine("msg", color)`

**MqttServiceImpl.cpp** (line 5, 14-18):
- Remove `#include "Ili9341Lcd.hpp"`
- Add `#include "DisplayStatus.hpp"`
- Remove local `lcdStatus()` function
- Replace all `lcdStatus("msg")` → `display::statusLine("msg")`

### 14. Build System — `Debug/Services/subdir.mk`

Add display include path:
```
-I../../../Shared/Inc/display
```
to `SERVICES_INCLUDES`. No new .cpp entries needed (all new code is header-only).

## Implementation Order

**Phase A: New files (no existing code broken, all header-only)** — DONE 2026-03-19
1. ~~Create `Shared/Inc/display/` directory~~
2. ~~Write `DisplayConfig.hpp` (feature flags — all non-core default OFF)~~
3. ~~Write `IDisplay.hpp` (with drawXBitmap + drawBitmap16 + ORANGE color)~~
4. ~~Write `TouchTypes.hpp` (TouchEvent + Gesture + KeyEvent) — guarded by `DISPLAY_FEATURE_TOUCH`~~
5. ~~Write `Widget.hpp` — guarded by `DISPLAY_FEATURE_WIDGETS`~~
6. ~~Write `BitmapButton.hpp` — guarded by `DISPLAY_FEATURE_BUTTON`~~
7. ~~Write `FormWidgets.hpp` — guarded by `DISPLAY_FEATURE_FORM`~~
8. ~~Write `DialogWidgets.hpp` — guarded by `DISPLAY_FEATURE_DIALOGS`~~
9. ~~Write `MutexDisplay.hpp` — guarded by `DISPLAY_FEATURE_MUTEX`~~
10. ~~Write `DisplayStatus.hpp` — guarded by `DISPLAY_FEATURE_STATUS`~~
11. ~~Write `Ili9341Display.hpp` (Services/Driver/)~~
12. ~~Write `ViewManager.hpp` — nav stack guarded by `DISPLAY_FEATURE_NAV_STACK`~~

**Phase B: Extend existing driver** — DONE 2026-03-19
13. ~~Add `drawXBitmap()` to `Ili9341Lcd.hpp` + `Ili9341Lcd.cpp`~~

**Phase C: Update include path** — DONE 2026-03-19
14. ~~Update `subdir.mk` — add `-I../../../Shared/Inc/display`~~
    - Note: CubeIDE uses per-directory subdir.mk (6 files updated, not just the consolidated one)

**Phase D: Migrate View layer (Ili9341Lcd → IDisplay)** — DONE 2026-03-19
15. ~~Update `LcdView.hpp` — `Ili9341Lcd&` → `display::IDisplay&`~~
16. ~~Update `MainView.hpp` — signatures + Input struct~~
17. ~~Update `MainView.cpp` — color constants (~25 replacements)~~
18. ~~Update `EcgBuffer.hpp` + `EcgDisplay.cpp` — pointer type + colors~~

**Phase E: Migrate Service layer** — DONE 2026-03-19
19. ~~Update `LcdService.hpp` — `getLcd()` → `getDisplay()`~~
20. ~~Update `LcdServiceImpl.hpp` + `.cpp` — add `Ili9341Display mAdapter` member~~
21. ~~Update `Controller.cpp` — `g_display` definition, wiring~~
    - MutexDisplay **deferred**: `FreeRtosMutex` costs ~88B RAM, board has only 56B headroom.
      `g_display` uses raw adapter (same thread-safety as before). Enable when RAM allows.

**Phase F: Unify ad-hoc status** — DONE 2026-03-19
22. ~~Update `AtsStorageServiceImpl.cpp` — `lcdStatus()` wraps `display::statusLine()`~~
23. ~~Update `SdBenchmarkServiceImpl.cpp` — `lcdMsg()` wraps `display::statusLine()`~~
24. ~~Update `MqttServiceImpl.cpp` — `lcdStatus()` wraps `display::statusLine()`~~

## Cost (Measured)

| Resource | Before | After | Delta |
|----------|--------|-------|-------|
| text | 108,984 | 110,592 | +1,608B (vtables + adapter delegation + drawXBitmap) |
| bss | 65,288 | 65,304 | +16B (g_display ptr + adapter ref) |
| data | 192 | 192 | 0 |
| Runtime | — | ~5ns/call | vtable dispatch (immeasurable vs FSMC I/O) |

**UNCHANGED files**: `LcdViewModel.hpp`, `Font5x7.hpp`
**MODIFIED** (additive only): `Ili9341Lcd.hpp/.cpp` — new `drawXBitmap()` method added

## Future Work (not in this PR)

| Task | Description |
|------|-------------|
| XPT2046 SPI driver | `Xpt2046Touch` in Services/Driver/, SPI1 or GPIO bit-bang, interrupt on PEN pin |
| GestureDetector | TouchEvent stream → Gesture recognition (tap threshold, swipe distance/velocity) |
| ViewManager touch wiring | Connect XPT2046 → GestureDetector → ViewManager.dispatchTouch/dispatchGesture |
| MainView.onTouch | Tap on temperature → toggle unit, tap on MQTT area → force reconnect, etc. |
| SettingsView | New view accessible via SwipeLeft from MainView |

Touch types (`TouchEvent`, `Gesture`) and LcdView virtual methods (`onTouch`, `onGesture`) are created **now** so the View interface is stable. Only the driver + detector are deferred.

## Verification

1. `make -j8 all` in `Targets/stm32f103ze/Debug/` — zero warnings
2. Flash to board + verify LCD renders identically (temp, ECG, stats, clock)
3. KEY2 safe eject: status messages via `g_display` work
4. KEY1 format: "Formatting..." message works
5. MQTT connect/disconnect: status line renders correctly
6. ECG waveform: smooth sweep, no flickering

---

## 優缺點分析

### 優點

| # | 優點 | 說明 |
|---|------|------|
| 1 | **完整硬體抽象** | View code 換 LCD 驅動完全不改（STM32/ESP32/Desktop） |
| 2 | **Header-only** | 11 個新檔全是 .hpp，不用改 build system（除了 drawXBitmap） |
| 3 | **低成本** | +500B Flash, +20B RAM，vtable dispatch ~5ns（vs FSMC I/O 微秒級） |
| 4 | **Thread-safe** | MutexDisplay decorator 解決 multi-task ad-hoc LCD 存取 |
| 5 | **一致性** | 3 個重複 lcdStatus 統一成 `display::statusLine()` |
| 6 | **完整 Widget** | HTML form 元素 1:1 對應，按鈕/滑桿/對話框都有 |
| 7 | **Focus 光棒** | 沒觸控也能用 KEY1/KEY2 操作所有 Widget |
| 8 | **Navigation Stack** | push/pop 多層導航，SwipeRight/KEY2 long 返回 |
| 9 | **Cross-platform** | TouchTypes/Widget/FormWidgets 在 Shared/ 下，ESP32/Linux 直接用 |

### 缺點

| # | 缺點 | 嚴重度 | 說明 |
|---|------|--------|------|
| 1 | ~~範圍膨脹~~ | ~~高~~ → 低 | `DisplayConfig.hpp` feature flags 解決 — 預設只開 Core+Mutex+Status，其他 `#if 0` 不編譯 |
| 2 | ~~YAGNI 風險~~ | ~~高~~ → 低 | Source code 在 tree 裡但不進 binary，改 flag `0→1` 即啟用，零浪費 |
| 3 | **觸控驅動不存在** | 中 | Widget 觸控功能無法在板子上測試，只能 code review |
| 4 | **繪圖原始** | 中 | Radio button 用方塊模擬圓形、無 anti-aliasing、字體只有 5x7 |
| 5 | **硬編碼 240px** | 中 | Dialog/StatusLine 寫死 240 寬度，換解析度要改 |
| 6 | **缺少 Scroll** | 低 | RadioGroup/List 超出螢幕沒有捲動機制 |
| 7 | **缺少 TextInput** | 低 | 電阻屏沒虛擬鍵盤，`<input type="text">` 不實際 |
| 8 | ~~Flash 膨脹~~ | ~~低~~ → 無 | Feature flag `0` 時完全不編譯，不佔 Flash/RAM |
| 9 | **virtual ~Widget()** | 低 | Cortex-M3 每個 vtable 多 4 bytes（-fno-rtti 環境可接受） |

### 建議：分階段實作

| 階段 | 內容 | 馬上需要? |
|------|------|-----------|
| **Phase 1** | IDisplay + Adapter + MutexDisplay + DisplayStatus + drawXBitmap | **DONE 2026-03-19** |
| **Phase 2** | LcdView/MainView/EcgBuffer 遷移 + Controller wiring | **DONE 2026-03-19** |
| **Phase 3** | TouchTypes + Widget + BitmapButton + ViewManager | **DONE 2026-03-19** (+24B text) |
| **Phase 4** | FormWidgets + DialogWidgets + Focus/光棒 | **DONE 2026-03-19** (+0B, gc-sections stripped) |
| **Phase 5** | Navigation Stack push/pop | **DONE 2026-03-19** (enabled with Phase 3) |

**Phase 1+2 已完成**（text +1.6KB, bss +16B）。MutexDisplay 因 RAM 限制暫緩，g_display 使用 raw adapter。
