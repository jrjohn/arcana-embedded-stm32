#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace light {

class LightService {
public:
    struct Output {
        Observable<LightDataModel>* DataEvents;
    };

    Output output;

    virtual ~LightService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    LightService() : output() {
        output.DataEvents = 0;
    }
};

} // namespace light
} // namespace arcana
