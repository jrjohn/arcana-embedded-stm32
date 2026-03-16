#pragma once

#include "LcdView.hpp"

namespace arcana {
namespace lcd {

/**
 * Main dashboard view — ECG waveform + storage stats + time.
 * Default view on boot.
 */
class MainView : public LcdView {
public:
    void onEnter(Ili9341Lcd& lcd) override;
    void render(Ili9341Lcd& lcd, const LcdOutput& output, LcdOutput& rendered) override;
    void renderEcgColumn(Ili9341Lcd& lcd, uint8_t x, uint8_t y, uint8_t prevY) override;

private:
    void renderTemp(Ili9341Lcd& lcd, const LcdOutput& out, LcdOutput& rendered);
    void renderSdInfo(Ili9341Lcd& lcd, const LcdOutput& out, LcdOutput& rendered);
    void renderStorage(Ili9341Lcd& lcd, const LcdOutput& out, LcdOutput& rendered);
    void renderTime(Ili9341Lcd& lcd, const LcdOutput& out, LcdOutput& rendered);

    static void uint32ToStr(char* buf, uint32_t value);

    static const uint16_t VALUE_X      = 20;
    static const uint16_t TEMP_VALUE_Y = 42;
    static const uint16_t SD_INFO_Y    = 82;
    static const uint16_t SD_STATUS_Y  = 96;
    static const uint16_t SD_RECORDS_Y = 112;
    static const uint16_t SD_RATE_Y    = 124;
    static const uint16_t MQTT_LABEL_Y = 142;
    static const uint16_t MQTT_STATUS_Y= 154;
    static const uint16_t ECG_TOP_Y    = 174;
    static const uint16_t ECG_HEIGHT   = 100;
    static const uint16_t ECG_WIDTH    = 240;
    static const uint16_t CLOCK_DATE_Y = 286;
    static const uint16_t CLOCK_TIME_Y = 304;
};

} // namespace lcd
} // namespace arcana
