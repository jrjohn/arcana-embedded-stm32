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

} // namespace display
} // namespace arcana

#endif // DISPLAY_FEATURE_STATUS
