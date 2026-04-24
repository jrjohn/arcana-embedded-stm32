#pragma once

#include "IDisplay.hpp"
#include "DisplayConfig.hpp"
#if DISPLAY_FEATURE_TOUCH
#include "TouchTypes.hpp"
#endif
#include "MainViewModel.hpp"

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
class BaseLcdView {
public:
    virtual ~BaseLcdView() {}

    /** Draw static layout (labels, borders, titles) */
    virtual void onEnter(display::IDisplay& lcd) = 0;

    /** Clean up before switching away */
    virtual void onExit(display::IDisplay& lcd) { (void)lcd; }

    /** Render changed data from ViewModel output */
    virtual void render(display::IDisplay& lcd, const LcdOutput& output, LcdOutput& rendered) = 0;

    /** Render single ECG column (called at 250Hz from timer) */
    virtual void renderEcgColumn(display::IDisplay& lcd, uint8_t x, uint8_t y, uint8_t prevY) {
        (void)lcd; (void)x; (void)y; (void)prevY;
    }

#if DISPLAY_FEATURE_TOUCH
    /** Handle touch event. Returns true if consumed. */
    virtual bool onTouch(const display::TouchEvent& event) { (void)event; return false; }

    /** Handle gesture (tap, swipe, long-press). Returns true if consumed. */
    virtual bool onGesture(display::Gesture gesture) { (void)gesture; return false; }

    /** Handle physical key event (KEY1/KEY2). Returns true if consumed. */
    virtual bool onKey(const display::KeyEvent& event) { (void)event; return false; }
#endif
};

} // namespace lcd
} // namespace arcana
