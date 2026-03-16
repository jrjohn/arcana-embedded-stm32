#pragma once

#include <cstdint>

// Forward declare LCD class for direct drawing
namespace arcana { namespace lcd { class Ili9341Lcd; } }

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

    lcd::Ili9341Lcd* lcd;
    uint16_t cursor;          // current X position (0-239)
    uint8_t  prevY;           // previous sample Y for line connection

    void init(lcd::Ili9341Lcd* lcdPtr) {
        lcd = lcdPtr;
        cursor = 0;
        prevY = 70;  // baseline
    }

    void pushAndDraw(uint8_t y);  // implemented in .cpp (needs Ili9341Lcd methods)
};

extern EcgDisplay g_ecgDisplay;

} // namespace arcana
