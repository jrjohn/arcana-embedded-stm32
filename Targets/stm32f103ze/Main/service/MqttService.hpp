#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
class Esp8266;
namespace wifi { class WifiService; }
namespace reg { class RegistrationService; }
namespace atsstorage { class AtsStorageService; }

namespace mqtt {

class MqttService {
public:
    struct Input {
        Observable<SensorDataModel>* SensorData;
        Observable<LightDataModel>*  LightData;
        wifi::WifiService*           Wifi;
        reg::RegistrationService*    Registration;
        atsstorage::AtsStorageService* Storage;       // timezone config
        uint8_t (*UploadPendingFiles)(Esp8266& esp);  // HTTPS upload (stateless)
    };

    struct Output {
        Observable<MqttCommandModel>*    CommandEvents;
        Observable<MqttConnectionModel>* ConnectionStatus;
    };

    Input  input;
    Output output;

    virtual ~MqttService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    MqttService() : input(), output() {
        input.SensorData = 0;
        input.LightData = 0;
        input.Wifi = 0;
        input.Registration = 0;
        input.Storage = 0;
        input.UploadPendingFiles = 0;
        output.CommandEvents = 0;
        output.ConnectionStatus = 0;
    }
};

} // namespace mqtt
} // namespace arcana
