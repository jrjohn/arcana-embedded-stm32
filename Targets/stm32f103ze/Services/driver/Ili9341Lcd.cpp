#include "Ili9341Lcd.hpp"
#include "Font5x7.hpp"

namespace arcana {
namespace lcd {


// Simple busy-wait delay (independent of HAL_Delay / TIM4)
static void delayMs(uint32_t ms) {
    // ~72MHz, -O0: each iteration ~9 cycles → 8000 iters ≈ 1ms
    volatile uint32_t i = 0;
    while (i < ms * 8000) { i = i + 1; }
}

// FSMC Bank1 NE4 (0x6C000000), A23 (PE2) as RS
// 16-bit mode: FSMC_A23 = HADDR[24], DATA offset = 1<<24 = 0x01000000
volatile uint16_t* const Ili9341Lcd::CMD_ADDR  = (volatile uint16_t*)0x6C000000;
volatile uint16_t* const Ili9341Lcd::DATA_ADDR = (volatile uint16_t*)0x6D000000;

Ili9341Lcd::Ili9341Lcd() {}

void Ili9341Lcd::writeCmd(uint16_t cmd) {
    *CMD_ADDR = cmd;
}

void Ili9341Lcd::writeData(uint16_t data) {
    *DATA_ADDR = data;
}

void Ili9341Lcd::initFsmc() {
    // Enable FSMC clock
    RCC->AHBENR |= RCC_AHBENR_FSMCEN;
    __IO uint32_t tmpreg = RCC->AHBENR;
    (void)tmpreg;

    // Small delay after clock enable
    HAL_Delay(1);

    // BCR4 (BTCR[6]): 16-bit SRAM mode, write enabled
    // MBKEN(bit0) | MWID=01(bit4, 16-bit) | WREN(bit12)
    FSMC_Bank1->BTCR[6] = 0x00001011;

    // BTR4 (BTCR[7]): conservative timing @ 72MHz
    // ADDSET=15 (~210ns), DATAST=60 (~840ns), BUSTURN=0
    FSMC_Bank1->BTCR[7] = (0 << 16) | (60 << 8) | (15 << 0);
}

void Ili9341Lcd::initSequence() {
    // Power Control B
    writeCmd(0xCF);
    writeData(0x00); writeData(0x81); writeData(0x30);

    // Power on Sequence Control
    writeCmd(0xED);
    writeData(0x64); writeData(0x03); writeData(0x12); writeData(0x81);

    // Driver Timing Control A
    writeCmd(0xE8);
    writeData(0x85); writeData(0x10); writeData(0x78);

    // Power Control A
    writeCmd(0xCB);
    writeData(0x39); writeData(0x2C); writeData(0x00);
    writeData(0x34); writeData(0x02);

    // Pump Ratio Control
    writeCmd(0xF7);
    writeData(0x20);

    // Driver Timing Control B
    writeCmd(0xEA);
    writeData(0x00); writeData(0x00);

    // Frame Rate Control
    writeCmd(0xB1);
    writeData(0x00); writeData(0x1B);

    // Display Function Control
    writeCmd(0xB6);
    writeData(0x0A); writeData(0xA2);

    // Power Control 1
    writeCmd(0xC0);
    writeData(0x25);

    // Power Control 2
    writeCmd(0xC1);
    writeData(0x10);

    // VCOM Control 1
    writeCmd(0xC5);
    writeData(0x45); writeData(0x45);

    // VCOM Control 2
    writeCmd(0xC7);
    writeData(0xA2);

    // Enable 3G
    writeCmd(0xF2);
    writeData(0x00);

    // Gamma Set
    writeCmd(0x26);
    writeData(0x01);

    // Positive Gamma Correction
    writeCmd(0xE0);
    writeData(0x0F); writeData(0x26); writeData(0x24); writeData(0x0B);
    writeData(0x0E); writeData(0x09); writeData(0x54); writeData(0xA8);
    writeData(0x46); writeData(0x0C); writeData(0x17); writeData(0x09);
    writeData(0x0F); writeData(0x07); writeData(0x00);

    // Negative Gamma Correction
    writeCmd(0xE1);
    writeData(0x00); writeData(0x19); writeData(0x1B); writeData(0x04);
    writeData(0x10); writeData(0x07); writeData(0x2A); writeData(0x47);
    writeData(0x39); writeData(0x03); writeData(0x06); writeData(0x06);
    writeData(0x30); writeData(0x38); writeData(0x0F);

    // Memory Access Control (portrait, BGR)
    writeCmd(0x36);
    writeData(0xC8);

    // Column address: 0-239
    writeCmd(0x2A);
    writeData(0x00); writeData(0x00);
    writeData(0x00); writeData(0xEF);

    // Page address: 0-319
    writeCmd(0x2B);
    writeData(0x00); writeData(0x00);
    writeData(0x01); writeData(0x3F);

    // Pixel Format: 16-bit RGB565
    writeCmd(0x3A);
    writeData(0x55);

    // Sleep Out
    writeCmd(0x11);
    delayMs(150);

    // Display On
    writeCmd(0x29);
}

void Ili9341Lcd::enableBacklight() {
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    // LCD Reset: PG11 HIGH → LOW → HIGH
    gpio.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOG, &gpio);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_11, GPIO_PIN_SET);
    delayMs(50);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_11, GPIO_PIN_RESET);
    delayMs(50);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_11, GPIO_PIN_SET);
    delayMs(50);

    // Backlight: PG6 LOW (active low = ON)
    gpio.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOG, &gpio);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6, GPIO_PIN_RESET);
}

uint16_t Ili9341Lcd::readId() {
    writeCmd(0xD3);
    volatile uint16_t dummy = *DATA_ADDR; // dummy read
    (void)dummy;
    volatile uint16_t id1 = *DATA_ADDR;   // 0x00
    volatile uint16_t id2 = *DATA_ADDR;   // 0x93
    volatile uint16_t id3 = *DATA_ADDR;   // 0x41
    (void)id1;
    return (id2 << 8) | (id3 & 0xFF);
}

void Ili9341Lcd::initHAL() {
    enableBacklight();
    initFsmc();
    delayMs(50);
    initSequence();
}

void Ili9341Lcd::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // Column address set
    writeCmd(0x2A);
    writeData(x0 >> 8); writeData(x0 & 0xFF);
    writeData(x1 >> 8); writeData(x1 & 0xFF);

    // Row address set
    writeCmd(0x2B);
    writeData(y0 >> 8); writeData(y0 & 0xFF);
    writeData(y1 >> 8); writeData(y1 & 0xFF);
}

void Ili9341Lcd::fillScreen(uint16_t color) {
    fillRect(0, 0, WIDTH, HEIGHT, color);
}

void Ili9341Lcd::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (x >= WIDTH || y >= HEIGHT) return;
    if (x + w > WIDTH) w = WIDTH - x;
    if (y + h > HEIGHT) h = HEIGHT - y;

    setWindow(x, y, x + w - 1, y + h - 1);
    writeCmd(0x2C);

    uint32_t total = (uint32_t)w * h;
    for (uint32_t i = 0; i < total; i++) {
        writeData(color);
    }
}

void Ili9341Lcd::drawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color) {
    fillRect(x, y, w, 1, color);
}

void Ili9341Lcd::drawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale) {
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t* glyph = FONT_5X7[c - 0x20];

    uint16_t cw = (FONT_WIDTH + 1) * scale;
    uint16_t ch = FONT_HEIGHT * scale;

    if (x + cw > WIDTH || y + ch > HEIGHT) return;

    setWindow(x, y, x + cw - 1, y + ch - 1);
    writeCmd(0x2C);

    for (uint8_t row = 0; row < FONT_HEIGHT; row++) {
        for (uint8_t sy = 0; sy < scale; sy++) {
            for (uint8_t col = 0; col < FONT_WIDTH + 1; col++) {
                uint16_t color = (col < FONT_WIDTH && (glyph[col] & (1 << row)))
                    ? fg : bg;
                for (uint8_t sx = 0; sx < scale; sx++) {
                    writeData(color);
                }
            }
        }
    }
}

void Ili9341Lcd::drawString(uint16_t x, uint16_t y, const char* str,
                            uint16_t fg, uint16_t bg, uint8_t scale) {
    uint16_t cw = (FONT_WIDTH + 1) * scale;
    while (*str) {
        if (x + cw > WIDTH) break;
        drawChar(x, y, *str, fg, bg, scale);
        x += cw;
        str++;
    }
}

void Ili9341Lcd::drawXBitmap(uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h,
                              const uint8_t* bitmap,
                              uint16_t fg, uint16_t bg) {
    if (x >= WIDTH || y >= HEIGHT) return;
    if (x + w > WIDTH) w = WIDTH - x;
    if (y + h > HEIGHT) h = HEIGHT - y;

    setWindow(x, y, x + w - 1, y + h - 1);
    writeCmd(0x2C);  // Memory Write

    uint16_t bytesPerRow = (w + 7) / 8;
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint8_t byte = bitmap[row * bytesPerRow + (col >> 3)];
            writeData((byte & (1 << (col & 7))) ? fg : bg);
        }
    }
}

} // namespace lcd
} // namespace arcana
