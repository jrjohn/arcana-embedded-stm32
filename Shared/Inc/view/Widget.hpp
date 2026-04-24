#pragma once
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_WIDGETS

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
    bool focusable;                 // true = can receive key focus

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

    /** Draw focus highlight border. Override for custom focus style. */
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
 * Call from View.onTouch() to route to correct widget via hit-test.
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
                    setFocus(display, (int8_t)i);
                    mPressedIndex = (int8_t)i;
                    return mWidgets[i]->handleTouch(display, event, tickCount);
                }
            }
            return false;
        }
        if (mPressedIndex >= 0) {
            bool consumed = mWidgets[mPressedIndex]->handleTouch(display, event, tickCount);
            if (event.type == TouchEvent::Up) mPressedIndex = -1;
            return consumed;
        }
        return false;
    }

    // ── Key / Focus handling ──

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
     * Key2 short press = focusNext, Key2 long press = focusPrev.
     * Key1 = forward to focused widget's onKey().
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
        if (mFocusIndex >= 0 && mWidgets[mFocusIndex]) {
            return mWidgets[mFocusIndex]->onKey(display, event);
        }
        return false;
    }

    int8_t focusIndex() const { return mFocusIndex; }

private:
    void setFocus(IDisplay& display, int8_t newIndex) {
        if (mFocusIndex >= 0 && mFocusIndex < mCount && mWidgets[mFocusIndex])
            mWidgets[mFocusIndex]->drawFocus(display, false);
        mFocusIndex = newIndex;
        if (mFocusIndex >= 0 && mFocusIndex < mCount && mWidgets[mFocusIndex])
            mWidgets[mFocusIndex]->drawFocus(display, true);
    }

    Widget* mWidgets[MAX_WIDGETS];
    uint8_t mCount;
    int8_t mPressedIndex;
    int8_t mFocusIndex;
};

} // namespace display
} // namespace arcana

#endif // DISPLAY_FEATURE_WIDGETS
