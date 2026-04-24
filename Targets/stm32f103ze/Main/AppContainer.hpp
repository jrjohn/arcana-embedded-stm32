#pragma once

#include "ITimerService.hpp"
#include "LedService.hpp"
#include "SensorService.hpp"
#include "LcdService.hpp"
#ifdef ARCANA_LIGHT_SENSOR
#include "LightService.hpp"
#endif
#include "SdBenchmarkService.hpp"
#include "AtsStorageService.hpp"
#include "WifiService.hpp"
#include "MqttService.hpp"
#include "BleService.hpp"
#include "OtaService.hpp"
#include "ServiceTypes.hpp"

// Forward declarations for future services
namespace arcana {
namespace diagnostic { class DiagnosticService; }
class CommandBridgeService;
}

namespace arcana {

class AppContainer {
public:
    static AppContainer& getInstance();

    void run();

private:
    AppContainer();
    ~AppContainer();
    AppContainer(const AppContainer&);
    AppContainer& operator=(const AppContainer&);

    void wireServices();
    void wireViews();
    void initHAL();
    void initServices();
    void startServices();

    timer::TimerService*             mTimer;
    led::LedService*                 mLed;
    sensor::SensorService*           mSensor;
    lcd::LcdService*                 mLcd;
#ifdef ARCANA_LIGHT_SENSOR
    light::LightService*             mLight;
#endif
    sdbench::SdBenchmarkService*     mSdBench;
    atsstorage::AtsStorageService*    mSdStorage;
    wifi::WifiService*               mWifi;
    mqtt::MqttService*               mMqtt;
    ble::BleService*                 mBle;
    OtaService*                      mOta;
};

} // namespace arcana
