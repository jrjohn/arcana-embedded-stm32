#pragma once

#include "stm32f1xx_hal.h"
#include <cstdint>

namespace arcana {
namespace lcd {

class Ili9341Lcd {
public:
    Ili9341Lcd();

    void initHAL();

    void fillScreen(uint16_t color);
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void drawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);
    void drawString(uint16_t x, uint16_t y, const char* str, uint16_t fg, uint16_t bg, uint8_t scale);
    void drawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);

    static const uint16_t WIDTH  = 240;
    static const uint16_t HEIGHT = 320;

    static const uint16_t BLACK   = 0x0000;
    static const uint16_t WHITE   = 0xFFFF;
    static const uint16_t RED     = 0xF800;
    static const uint16_t GREEN   = 0x07E0;
    static const uint16_t BLUE    = 0x001F;
    static const uint16_t YELLOW  = 0xFFE0;
    static const uint16_t CYAN    = 0x07FF;
    static const uint16_t MAGENTA = 0xF81F;
    static const uint16_t GRAY    = 0x7BEF;
    static const uint16_t DARKGRAY = 0x39E7;

private:
    void enableBacklight();
    void initFsmc();
    uint16_t readId();
    void initSequence();
    void writeCmd(uint16_t cmd);
    void writeData(uint16_t data);
    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

    // FSMC Bank1 NE4 base: 0x6C000000, A23 (PE2) as RS (D/C)
    // CMD (A23=0): 0x6C000000, DATA (A23=1): 0x6D000000
    static volatile uint16_t* const CMD_ADDR;
    static volatile uint16_t* const DATA_ADDR;
};

} // namespace lcd
} // namespace arcana
