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
    static void onLightData(LightDataModel* model, void* ctx);
    static void onStorageStats(StorageStatsModel* model, void* ctx);
    void updateSensorDisplay(const SensorDataModel* data);
    void updateLightDisplay(const LightDataModel* data);
    void updateStorageDisplay(const StorageStatsModel* data);
    void drawInitialScreen();

    static void intToStr(char* buf, int value);
    static void uint32ToStr(char* buf, uint32_t value);

    Ili9341Lcd mLcd;

    static const uint16_t TEMP_VALUE_Y = 100;
    static const uint16_t LIGHT_VALUE_Y = 175;
    static const uint16_t STORAGE_VALUE_Y = 260;
    static const uint16_t VALUE_X = 20;
};

} // namespace lcd
} // namespace arcana
