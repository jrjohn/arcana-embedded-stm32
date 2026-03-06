#pragma once

#include "LcdService.hpp"
#include "Ili9341Lcd.hpp"

namespace arcana {
namespace lcd {

class LcdServiceImpl : public LcdService {
public:
    static LcdService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

private:
    LcdServiceImpl();
    ~LcdServiceImpl();
    LcdServiceImpl(const LcdServiceImpl&);
    LcdServiceImpl& operator=(const LcdServiceImpl&);

    static void onSensorData(SensorDataModel* model, void* ctx);
    void updateSensorDisplay(const SensorDataModel* data);
    void drawInitialScreen();

    static void intToStr(char* buf, int value);

    Ili9341Lcd mLcd;

    static const uint16_t TEMP_VALUE_Y = 120;
    static const uint16_t HUMI_VALUE_Y = 220;
    static const uint16_t VALUE_X = 20;
};

} // namespace lcd
} // namespace arcana
