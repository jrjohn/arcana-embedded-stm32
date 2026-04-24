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

// ── Toast — request/render split (single-writer: render task only) ──

/** ToastRequest: any task writes here (data only, no LCD access) */
struct ToastRequest {
    char text[24];           // internal buffer (no dangling pointers)
    Color fg, bg;
    uint32_t durationMs;
    volatile bool pending;   // new toast to show
    volatile bool dismiss;   // request to clear
};

inline ToastRequest& toastRequest() {
    static ToastRequest s = {{}, colors::WHITE, colors::DARKGRAY, 0, false, false};
    return s;
}

/** Request toast from any task (thread-safe: only writes data, no LCD) */
inline void requestToast(const char* msg, uint32_t durationMs,
                         Color fg = colors::WHITE, Color bg = colors::DARKGRAY) {
    ToastRequest& r = toastRequest();
    // Copy message into internal buffer
    uint8_t i = 0;
    while (i < 23 && msg[i]) { r.text[i] = msg[i]; i++; }
    r.text[i] = '\0';
    r.fg = fg;
    r.bg = bg;
    r.durationMs = durationMs;
    r.pending = true;
}

/** Dismiss toast from any task */
inline void dismissToast() {
    toastRequest().dismiss = true;
}

/** ToastState: render-task internal state (not accessed by other tasks) */
struct ToastState {
    uint16_t x, y, w, h;
    uint16_t textX, textY;
    uint32_t dismissTick;
    Color fg, bg;
    uint8_t scale;
    bool active;
};

inline ToastState& toastState() {
    static ToastState s = {};
    return s;
}

/** Render-task only: draw the toast box + text */
inline void toastRedraw(const ToastState& ts, const char* text) {
    if (!g_display) return;
    g_display->fillRect(ts.x, ts.y, ts.w, ts.h, ts.bg);
    g_display->drawHLine(ts.x, ts.y, ts.w, colors::WHITE);
    g_display->drawHLine(ts.x, ts.y + ts.h - 1, ts.w, colors::WHITE);
    g_display->fillRect(ts.x, ts.y, 1, ts.h, colors::WHITE);
    g_display->fillRect(ts.x + ts.w - 1, ts.y, 1, ts.h, colors::WHITE);
    g_display->drawString(ts.textX, ts.textY, text, ts.fg, ts.bg, ts.scale);
}

/** Render-task only: process toast requests + lifecycle.
 *  Returns true if underlying area needs redraw (toast dismissed). */
inline bool toastUpdate(uint32_t currentTick) {
    ToastRequest& req = toastRequest();
    ToastState& ts = toastState();

    // Handle dismiss request
    if (req.dismiss) {
        req.dismiss = false;
        req.pending = false;
        if (ts.active && g_display) {
            g_display->fillRect(ts.x, ts.y, ts.w, ts.h, colors::BLACK);
        }
        ts.active = false;
        return true;
    }

    // Handle new toast request
    if (req.pending) {
        req.pending = false;
        uint8_t scale = 2;
        uint16_t charW = 6 * scale;
        uint16_t textW = 0;
        const char* p = req.text;
        while (*p++) textW += charW;
        uint16_t textH = 7 * scale;
        uint16_t padX = 16, padY = 12;
        uint16_t boxW = textW + padX * 2;
        uint16_t boxH = textH + padY * 2;
        if (boxW < 200) boxW = 200;
        if (g_display) {
            uint16_t bx = (g_display->width() - boxW) / 2;
            uint16_t by = (g_display->height() - boxH) / 2;
            ts.x = bx; ts.y = by; ts.w = boxW; ts.h = boxH;
            ts.textX = bx + (boxW - textW) / 2; ts.textY = by + padY;
        }
        ts.dismissTick = currentTick + req.durationMs;
        ts.fg = req.fg; ts.bg = req.bg; ts.scale = 2;
        ts.active = true;
        toastRedraw(ts, req.text);
        return false;
    }

    // Active toast: check expiry or repaint
    if (!ts.active) return false;
    if (currentTick >= ts.dismissTick) {
        ts.active = false;
        if (g_display) {
            g_display->fillRect(ts.x, ts.y, ts.w, ts.h, colors::BLACK);
        }
        return true;
    }
    toastRedraw(ts, toastRequest().text);
    return false;
}

// Backward-compatible wrappers (redirect to requestToast)
inline void toast(const char* msg, uint32_t durationMs, uint32_t /*currentTick*/,
                  Color fg = colors::WHITE, Color bg = colors::DARKGRAY, uint8_t /*scale*/ = 2) {
    requestToast(msg, durationMs, fg, bg);
}

inline void clearToast() { dismissToast(); }

} // namespace display
} // namespace arcana

#endif // DISPLAY_FEATURE_STATUS
