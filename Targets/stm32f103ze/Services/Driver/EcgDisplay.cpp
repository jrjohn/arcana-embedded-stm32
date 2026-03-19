#include "EcgBuffer.hpp"

namespace arcana {

EcgDisplay g_ecgDisplay = {};

void EcgDisplay::pushAndDraw(uint8_t y) {
    if (!lcd) return;
    if (y >= HEIGHT) y = HEIGHT - 1;

    // Erase ahead: clear the next 4 columns (creates the "gap" cursor effect)
    uint16_t eraseX = (cursor + 1) % WIDTH;
    for (int i = 0; i < 4; i++) {
        lcd->fillRect(eraseX, TOP_Y, 1, HEIGHT, display::colors::BLACK);
        eraseX = (eraseX + 1) % WIDTH;
    }

    // Draw vertical line from prevY to y (connected trace)
    uint8_t minY = (prevY < y) ? prevY : y;
    uint8_t maxY = (prevY > y) ? prevY : y;
    uint16_t lineH = maxY - minY + 1;
    if (lineH < 2) lineH = 2;  // minimum 2px thick for visibility

    lcd->fillRect(cursor, TOP_Y + minY, 1, lineH, display::colors::GREEN);

    prevY = y;
    cursor = (cursor + 1) % WIDTH;
}

} // namespace arcana
