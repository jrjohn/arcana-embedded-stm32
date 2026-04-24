#pragma once
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_DIALOGS

#include "IDisplay.hpp"
#include "TouchTypes.hpp"

namespace arcana {
namespace display {

/**
 * AlertDialog — modal popup with title, message, [OK] button.
 * Dismisses on OK tap or Key1 press.
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
        display.fillRect(x, y, DLG_W, DLG_H, colors::BLACK);
        display.drawHLine(x, y, DLG_W, colors::WHITE);
        display.drawHLine(x, y + DLG_H - 1, DLG_W, colors::WHITE);
        display.fillRect(x, y, 1, DLG_H, colors::WHITE);
        display.fillRect(x + DLG_W - 1, y, 1, DLG_H, colors::WHITE);
        display.drawString(x + 8, y + 6, title, colors::YELLOW, colors::BLACK, 1);
        display.drawHLine(x + 4, y + 16, DLG_W - 8, colors::DARKGRAY);
        display.drawString(x + 8, y + 22, message, colors::WHITE, colors::BLACK, 1);
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
        return true;
    }

    bool handleKey(IDisplay& display, const KeyEvent& event) {
        if (!active) return false;
        if (event.type == KeyEvent::Press && event.key == KeyEvent::Key1) {
            dismiss(display);
        }
        return true;
    }

private:
    uint16_t mBtnX, mBtnY;
    void dismiss(IDisplay& display) {
        (void)display;
        active = false;
        if (onDismiss) onDismiss(context);
    }
};

/**
 * ConfirmDialog — modal with title, message, [OK] + [Cancel].
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
    uint8_t focusBtn;  // 0=Cancel, 1=OK

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
        display.fillRect(x, y, DLG_W, DLG_H, colors::BLACK);
        display.drawHLine(x, y, DLG_W, colors::WHITE);
        display.drawHLine(x, y + DLG_H - 1, DLG_W, colors::WHITE);
        display.fillRect(x, y, 1, DLG_H, colors::WHITE);
        display.fillRect(x + DLG_W - 1, y, 1, DLG_H, colors::WHITE);
        display.drawString(x + 8, y + 6, title, colors::YELLOW, colors::BLACK, 1);
        display.drawHLine(x + 4, y + 16, DLG_W - 8, colors::DARKGRAY);
        display.drawString(x + 8, y + 22, message, colors::WHITE, colors::BLACK, 1);
        mCancelX = x + DLG_W / 2 - BTN_W - 10;
        mBtnY = y + DLG_H - BTN_H - 6;
        Color cancelBg = (focusBtn == 0) ? colors::CYAN : colors::DARKGRAY;
        display.fillRect(mCancelX, mBtnY, BTN_W, BTN_H, cancelBg);
        display.drawString(mCancelX + 4, mBtnY + 6, "Cancel", colors::WHITE, cancelBg, 1);
        mOkX = x + DLG_W / 2 + 10;
        Color okBg = (focusBtn == 1) ? colors::CYAN : colors::DARKGRAY;
        display.fillRect(mOkX, mBtnY, BTN_W, BTN_H, okBg);
        display.drawString(mOkX + 16, mBtnY + 6, "OK", colors::WHITE, okBg, 1);
    }

    bool handleTouch(IDisplay& display, const TouchEvent& event) {
        (void)display;
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
        return true;
    }

    bool handleKey(IDisplay& display, const KeyEvent& event) {
        if (!active) return false;
        if (event.type == KeyEvent::Press) {
            if (event.key == KeyEvent::Key2) {
                focusBtn = 1 - focusBtn;
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
 */
class Toast {
public:
    bool active;
    uint32_t dismissTick;

    Toast() : active(false), dismissTick(0) {}

    void show(IDisplay& display, const char* msg, uint32_t durationMs,
              uint32_t currentTick, Color color = colors::WHITE) {
        active = true;
        dismissTick = currentTick + durationMs;
        uint16_t msgW = 0;
        const char* p = msg; while (*p++) msgW += 6;
        uint16_t x = (display.width() - msgW) / 2;
        uint16_t y = display.height() - 24;
        mX = x - 4; mY = y - 4; mW = msgW + 8; mH = 16;
        display.fillRect(mX, mY, mW, mH, colors::DARKGRAY);
        display.drawString(x, y, msg, color, colors::DARKGRAY, 1);
    }

    bool update(IDisplay& display, uint32_t currentTick) {
        if (active && currentTick >= dismissTick) {
            active = false;
            display.fillRect(mX, mY, mW, mH, colors::BLACK);
            return true;
        }
        return false;
    }

private:
    uint16_t mX, mY, mW, mH;
};

} // namespace display
} // namespace arcana

#endif // DISPLAY_FEATURE_DIALOGS
