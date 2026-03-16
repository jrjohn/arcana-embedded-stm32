#pragma once

#include "ITimerService.hpp"
#include "LedService.hpp"
#include "SensorService.hpp"
#include "LcdService.hpp"
#include "LightService.hpp"
#include "SdBenchmarkService.hpp"
#include "AtsStorageService.hpp"
#include "WifiService.hpp"
#include "MqttService.hpp"
#include "BleService.hpp"
#include "ServiceTypes.hpp"

// Forward declarations for future services
namespace arcana {
namespace diagnostic { class DiagnosticService; }
class CommandBridgeService;
}

namespace arcana {

class Controller {
public:
    static Controller& getInstance();

    void run();

private:
    Controller();
    ~Controller();
    Controller(const Controller&);
    Controller& operator=(const Controller&);

    void wireServices();
    void wireViews();
    void initHAL();
    void initServices();
    void startServices();

    timer::TimerService*             mTimer;
    led::LedService*                 mLed;
    sensor::SensorService*           mSensor;
    lcd::LcdService*                 mLcd;
    light::LightService*             mLight;
    sdbench::SdBenchmarkService*     mSdBench;
    atsstorage::AtsStorageService*    mSdStorage;
    wifi::WifiService*               mWifi;
    mqtt::MqttService*               mMqtt;
    ble::BleService*                 mBle;
};

} // namespace arcana
