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
    static void onSdBenchmark(SdBenchmarkModel* model, void* ctx);
    static void onBaseTimer(TimerModel* model, void* ctx);
    void updateSensorDisplay(const SensorDataModel* data);
    void updateLightDisplay(const LightDataModel* data);
    void updateStorageDisplay(const StorageStatsModel* data);
    void updateSdBenchmarkDisplay(const SdBenchmarkModel* data);
    void updateTimeDisplay();
    void drawInitialScreen();
    void drawEcgWaveform();

    static void intToStr(char* buf, int value);
    static void uint32ToStr(char* buf, uint32_t value);

    Ili9341Lcd mLcd;
    uint8_t mEcgPrevY[240];  // previous Y for each column (for erase)

    static const uint16_t TEMP_VALUE_Y = 42;
    static const uint16_t SD_SPEED_Y = 80;
    static const uint16_t SD_TOTAL_Y = 100;
    static const uint16_t SD_RECORDS_Y = 112;
    static const uint16_t SD_RATE_Y = 124;
    static const uint16_t MQTT_LABEL_Y = 142;
    static const uint16_t MQTT_STATUS_Y = 154;
    static const uint16_t ECG_TOP_Y = 174;
    static const uint16_t ECG_HEIGHT = 100;
    static const uint16_t ECG_WIDTH = 240;
    static const uint16_t CLOCK_DATE_Y = 286;
    static const uint16_t CLOCK_TIME_Y = 304;
    static const uint16_t VALUE_X = 20;
};

} // namespace lcd
} // namespace arcana
