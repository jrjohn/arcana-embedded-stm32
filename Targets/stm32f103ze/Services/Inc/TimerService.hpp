#pragma once

#include "Observable.hpp"
#include "Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace timer {

class TimerService {
public:
    struct Output {
        Observable<TimerModel>* FastTimer;
        Observable<TimerModel>* BaseTimer;
    };

    Output output;

    virtual ~TimerService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    TimerService() : output() {
        output.FastTimer = 0;
        output.BaseTimer = 0;
    }
};

} // namespace timer
} // namespace arcana
