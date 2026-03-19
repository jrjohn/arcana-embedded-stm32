#pragma once
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_STATUS

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

// ── Toast — centered overlay with background box ──

struct ToastState {
    uint16_t x, y, w, h;       // drawn rect (for clearing)
    uint16_t textX, textY;     // text position
    uint32_t dismissTick;       // tick when toast expires
    const char* msg;            // message pointer (must be static/literal)
    Color fg, bg;
    uint8_t scale;
    bool active;
};

/** Global toast state (one toast at a time) */
inline ToastState& toastState() {
    static ToastState s = {};
    return s;
}

/** Redraw the toast box + text (used internally by toast() and toastUpdate()) */
inline void toastRedraw(const ToastState& ts) {
    if (!g_display || !ts.msg) return;
    g_display->fillRect(ts.x, ts.y, ts.w, ts.h, ts.bg);
    g_display->drawHLine(ts.x, ts.y, ts.w, colors::WHITE);
    g_display->drawHLine(ts.x, ts.y + ts.h - 1, ts.w, colors::WHITE);
    g_display->fillRect(ts.x, ts.y, 1, ts.h, colors::WHITE);
    g_display->fillRect(ts.x + ts.w - 1, ts.y, 1, ts.h, colors::WHITE);
    g_display->drawString(ts.textX, ts.textY, ts.msg, ts.fg, ts.bg, ts.scale);
}

/** Show centered toast message (scale 2 = large text).
 *  msg must point to a string literal or static buffer. */
inline void toast(const char* msg, uint32_t durationMs, uint32_t currentTick,
                  Color fg = colors::WHITE, Color bg = colors::DARKGRAY, uint8_t scale = 2) {
    if (!g_display) return;
    uint16_t charW = 6 * scale;
    uint16_t textW = 0;
    const char* p = msg;
    while (*p++) textW += charW;
    uint16_t textH = 7 * scale;
    uint16_t padX = 16;
    uint16_t padY = 12;
    uint16_t boxW = textW + padX * 2;
    uint16_t boxH = textH + padY * 2;
    // Minimum width: cover most of screen to avoid underlying text peeking
    if (boxW < 200) boxW = 200;
    uint16_t bx = (g_display->width() - boxW) / 2;
    uint16_t by = (g_display->height() - boxH) / 2;
    // Save state (repaint-on-top: no HW layers, redraw every update cycle)
    ToastState& ts = toastState();
    ts.x = bx; ts.y = by; ts.w = boxW; ts.h = boxH;
    // Center text within box
    ts.textX = bx + (boxW - textW) / 2; ts.textY = by + padY;
    ts.dismissTick = currentTick + durationMs;
    ts.msg = msg; ts.fg = fg; ts.bg = bg; ts.scale = scale;
    ts.active = true;
    toastRedraw(ts);
}

/** Call periodically (~50ms). Redraws while active, clears when expired. */
inline bool toastUpdate(uint32_t currentTick) {
    ToastState& ts = toastState();
    if (!ts.active) return false;
    if (currentTick >= ts.dismissTick) {
        ts.active = false;
        if (g_display) {
            g_display->fillRect(ts.x, ts.y, ts.w, ts.h, colors::BLACK);
        }
        return true;  // expired — underlying area needs redraw
    }
    // Repaint-on-top: redraw toast over any render updates
    toastRedraw(ts);
    return false;
}

inline void clearToast() {
    ToastState& ts = toastState();
    if (ts.active && g_display) {
        g_display->fillRect(ts.x, ts.y, ts.w, ts.h, colors::BLACK);
    }
    ts.active = false;
}

} // namespace display
} // namespace arcana

#endif // DISPLAY_FEATURE_STATUS
