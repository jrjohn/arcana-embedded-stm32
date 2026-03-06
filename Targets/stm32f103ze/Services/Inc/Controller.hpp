#pragma once

#include "TimerService.hpp"
#include "LedService.hpp"
#include "SensorService.hpp"
#include "LcdService.hpp"
#include "ServiceTypes.hpp"

// Forward declarations for future services
namespace arcana {
namespace mqtt { class MqttTransportService; }
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
    // mqtt::MqttTransportService*   mMqtt;
    // CommandBridgeService*         mBridge;
    // diagnostic::DiagnosticService* mDiag;
};

} // namespace arcana
