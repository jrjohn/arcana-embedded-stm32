#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace lcd {

class LcdService {
public:
    struct Input {
        Observable<SensorDataModel>* SensorData;
    };

    Input input;

    virtual ~LcdService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    LcdService() : input() {
        input.SensorData = 0;
    }
};

} // namespace lcd
} // namespace arcana
