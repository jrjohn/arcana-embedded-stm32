#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace sensor {

class SensorService {
public:
    struct Output {
        Observable<SensorDataModel>* DataEvents;
    };

    Output output;

    virtual ~SensorService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    SensorService() : output() {
        output.DataEvents = 0;
    }
};

} // namespace sensor
} // namespace arcana
