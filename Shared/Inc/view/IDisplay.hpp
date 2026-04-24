#pragma once
#include <cstdint>

namespace arcana {
namespace display {

using Color = uint16_t;  // RGB565

namespace colors {
    static const Color BLACK    = 0x0000;
    static const Color WHITE    = 0xFFFF;
    static const Color RED      = 0xF800;
    static const Color GREEN    = 0x07E0;
    static const Color BLUE     = 0x001F;
    static const Color YELLOW   = 0xFFE0;
    static const Color CYAN     = 0x07FF;
    static const Color MAGENTA  = 0xF81F;
    static const Color GRAY     = 0x7BEF;
    static const Color DARKGRAY = 0x39E7;
    static const Color ORANGE   = 0xFD20;
}

class IDisplay {
public:
    virtual ~IDisplay() {}

    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;

    virtual void fillScreen(Color color) = 0;
    virtual void fillRect(uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h, Color color) = 0;
    virtual void drawHLine(uint16_t x, uint16_t y,
                           uint16_t w, Color color) {
        fillRect(x, y, w, 1, color);
    }
    virtual void drawChar(uint16_t x, uint16_t y, char c,
                          Color fg, Color bg, uint8_t scale) = 0;
    virtual void drawString(uint16_t x, uint16_t y, const char* str,
                            Color fg, Color bg, uint8_t scale) = 0;

    /** Draw 1bpp XBM bitmap (same format as u8g2_DrawXBMP, LSB first) */
    virtual void drawXBitmap(uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const uint8_t* bitmap,
                             Color fg, Color bg) = 0;

    /** Draw RGB565 raw pixel buffer (default: slow per-pixel, override for DMA) */
    virtual void drawBitmap16(uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h,
                              const uint16_t* pixels) {
        for (uint16_t r = 0; r < h; r++)
            for (uint16_t c = 0; c < w; c++)
                fillRect(x + c, y + r, 1, 1, pixels[r * w + c]);
    }
};

/** Thread-safe global display for ad-hoc status callers */
extern IDisplay* g_display;

} // namespace display
} // namespace arcana
