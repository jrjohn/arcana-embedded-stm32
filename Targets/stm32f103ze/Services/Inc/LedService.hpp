#pragma once

#include "Observable.hpp"
#include "Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace led {

class LedService {
public:
    struct Input {
        Observable<TimerModel>* TimerEvents;
    };

    Input input;

    virtual ~LedService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    LedService() : input() {
        input.TimerEvents = 0;
    }
};

} // namespace led
} // namespace arcana
