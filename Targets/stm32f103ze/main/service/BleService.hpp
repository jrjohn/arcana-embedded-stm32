#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace ble {

/**
 * BLE Service — sensor streaming + command transport via HC-08.
 * Pushes sensor JSON to phone, receives framed commands.
 */
class BleService {
public:
    struct Input {
        Observable<SensorDataModel>* SensorData;
        Observable<LightDataModel>*  LightData;
    };

    Input input;

    virtual ~BleService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    BleService() : input() {
        input.SensorData = 0;
        input.LightData = 0;
    }
};

} // namespace ble
} // namespace arcana
