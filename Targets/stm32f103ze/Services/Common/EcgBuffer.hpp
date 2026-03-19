#pragma once

#include <cstdint>
#include "IDisplay.hpp"

namespace arcana {

/**
 * Real-time ECG sweep display.
 * Draws one column at a time directly to LCD from the producer task.
 * Sweep: 240 columns, ~960ms per sweep at 250Hz update.
 */
struct EcgDisplay {
    static const uint16_t WIDTH  = 240;
    static const uint16_t TOP_Y  = 174;
    static const uint16_t HEIGHT = 100;

    display::IDisplay* lcd;
    uint16_t cursor;          // current X position (0-239)
    uint8_t  prevY;           // previous sample Y for line connection

    void init(display::IDisplay* lcdPtr) {
        lcd = lcdPtr;
        cursor = 0;
        prevY = 70;  // baseline
    }

    void pushAndDraw(uint8_t y);  // implemented in .cpp (needs IDisplay methods)
};

extern EcgDisplay g_ecgDisplay;

} // namespace arcana
