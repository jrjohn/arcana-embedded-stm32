#pragma once
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_TOUCH

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

#endif // DISPLAY_FEATURE_TOUCH
