#pragma once

#include "ITimerService.hpp"
#include "LedService.hpp"
#include "SensorService.hpp"
#include "LcdService.hpp"
#include "LightService.hpp"
#include "StorageService.hpp"
#include "SdBenchmarkService.hpp"
#include "SdStorageService.hpp"
#include "WifiService.hpp"
#include "MqttService.hpp"
#include "AdcSimulatorService.hpp"
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
    void initHAL();
    void initServices();
    void startServices();

    timer::TimerService*             mTimer;
    led::LedService*                 mLed;
    sensor::SensorService*           mSensor;
    lcd::LcdService*                 mLcd;
    light::LightService*             mLight;
    storage::StorageService*         mStorage;
    sdbench::SdBenchmarkService*     mSdBench;
    sdstorage::SdStorageService*     mSdStorage;
    wifi::WifiService*               mWifi;
    mqtt::MqttService*               mMqtt;
    AdcSimulatorService*             mAdcSim;
    // CommandBridgeService*         mBridge;
    // diagnostic::DiagnosticService* mDiag;
};

} // namespace arcana
