#pragma once
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_BUTTON

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

#endif // DISPLAY_FEATURE_BUTTON
