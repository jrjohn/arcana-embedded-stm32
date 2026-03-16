#pragma once

#include "Ili9341Lcd.hpp"
#include "LcdViewModel.hpp"

namespace arcana {
namespace lcd {

/**
 * Abstract LCD view — base class for all screen layouts.
 *
 * Lifecycle:
 *   onEnter() → called when view becomes active (draw static layout)
 *   render()  → called when ViewModel output changes (draw dynamic data)
 *   onExit()  → called when switching to another view
 *
 * Future views: SettingsView, ChartView, DetailView, etc.
 * Switch via button (KEY1/KEY2) or touch input.
 */
class LcdView {
public:
    virtual ~LcdView() {}

    /** Draw static layout (labels, borders, titles) */
    virtual void onEnter(Ili9341Lcd& lcd) = 0;

    /** Clean up before switching away */
    virtual void onExit(Ili9341Lcd& lcd) { (void)lcd; }

    /** Render changed data from ViewModel output */
    virtual void render(Ili9341Lcd& lcd, const LcdOutput& output, LcdOutput& rendered) = 0;

    /** Render single ECG column (called at 250Hz from timer) */
    virtual void renderEcgColumn(Ili9341Lcd& lcd, uint8_t x, uint8_t y, uint8_t prevY) {
        (void)lcd; (void)x; (void)y; (void)prevY;
    }
};

} // namespace lcd
} // namespace arcana
