#pragma once
#include "IDisplay.hpp"
#include "Ili9341Lcd.hpp"

namespace arcana {
namespace display {

class Ili9341Display : public IDisplay {
    lcd::Ili9341Lcd& mLcd;
public:
    explicit Ili9341Display(lcd::Ili9341Lcd& lcd) : mLcd(lcd) {}

    uint16_t width()  const override { return lcd::Ili9341Lcd::WIDTH; }
    uint16_t height() const override { return lcd::Ili9341Lcd::HEIGHT; }

    void fillScreen(Color c) override { mLcd.fillScreen(c); }
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, Color c) override {
        mLcd.fillRect(x, y, w, h, c);
    }
    void drawHLine(uint16_t x, uint16_t y, uint16_t w, Color c) override {
        mLcd.drawHLine(x, y, w, c);
    }
    void drawChar(uint16_t x, uint16_t y, char c, Color fg, Color bg, uint8_t s) override {
        mLcd.drawChar(x, y, c, fg, bg, s);
    }
    void drawString(uint16_t x, uint16_t y, const char* str, Color fg, Color bg, uint8_t s) override {
        mLcd.drawString(x, y, str, fg, bg, s);
    }
    void drawXBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint8_t* bitmap, Color fg, Color bg) override {
        mLcd.drawXBitmap(x, y, w, h, bitmap, fg, bg);
    }
};

} // namespace display
} // namespace arcana
