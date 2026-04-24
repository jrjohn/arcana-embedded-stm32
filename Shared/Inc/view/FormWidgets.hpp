#pragma once
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_FORM

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
        focusable = false;
    }

    void setup(uint16_t x_, uint16_t y_, const char* txt,
               Color fg_ = colors::WHITE, Color bg_ = colors::BLACK, uint8_t s = 1) {
        x = x_; y = y_; text = txt; fg = fg_; bg = bg_; scale = s;
        w = 0; h = scale * 7;
    }

    void draw(IDisplay& display) override {
        display.drawString(x, y, text, fg, bg, scale);
    }

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
    const char* label;

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
    }

    void draw(IDisplay& display) override {
        display.fillRect(x, y, BOX_SIZE, BOX_SIZE, bg);
        display.drawHLine(x, y, BOX_SIZE, fg);
        display.drawHLine(x, y + BOX_SIZE - 1, BOX_SIZE, fg);
        display.fillRect(x, y, 1, BOX_SIZE, fg);
        display.fillRect(x + BOX_SIZE - 1, y, 1, BOX_SIZE, fg);
        if (checked) {
            display.fillRect(x + 3, y + 3, BOX_SIZE - 6, BOX_SIZE - 6, checkColor);
        }
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
            display.fillRect(x, oy + 2, DOT_SIZE, DOT_SIZE, bg);
            display.drawHLine(x + 2, oy + 2, DOT_SIZE - 4, fg);
            display.drawHLine(x + 2, oy + DOT_SIZE + 1, DOT_SIZE - 4, fg);
            display.fillRect(x, oy + 4, 1, DOT_SIZE - 4, fg);
            display.fillRect(x + DOT_SIZE - 1, oy + 4, 1, DOT_SIZE - 4, fg);
            if (i == selected) {
                display.fillRect(x + 3, oy + 5, DOT_SIZE - 6, DOT_SIZE - 6, selectColor);
            }
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
        display.fillRect(x, y, w, h, bg);
        uint16_t trackY = y + (h - TRACK_H) / 2;
        display.fillRect(x, trackY, w, TRACK_H, trackColor);
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
    uint8_t percent;
    Color fillColor, trackColor, borderColor, bg;

    ProgressBar() : Widget(), percent(0),
        fillColor(colors::GREEN), trackColor(colors::DARKGRAY),
        borderColor(colors::WHITE), bg(colors::BLACK) {
        focusable = false;
    }

    void setup(uint16_t x_, uint16_t y_, uint16_t w_, uint16_t h_) {
        x = x_; y = y_; w = w_; h = h_;
    }

    void setPercent(IDisplay& display, uint8_t pct) {
        percent = (pct > 100) ? 100 : pct;
        draw(display);
    }

    void draw(IDisplay& display) override {
        display.drawHLine(x, y, w, borderColor);
        display.drawHLine(x, y + h - 1, w, borderColor);
        display.fillRect(x, y, 1, h, borderColor);
        display.fillRect(x + w - 1, y, 1, h, borderColor);
        uint16_t fillW = (uint32_t)(w - 2) * percent / 100;
        if (fillW > 0)
            display.fillRect(x + 1, y + 1, fillW, h - 2, fillColor);
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
        display.fillRect(x, y, BTN_W, h, btnColor);
        display.drawString(x + 6, y + 4, "-", fg, btnColor, 1);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", (int)value);
        display.fillRect(x + BTN_W, y, VAL_W, h, bg);
        display.drawString(x + BTN_W + 8, y + 4, buf, fg, bg, 1);
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

#endif // DISPLAY_FEATURE_FORM
