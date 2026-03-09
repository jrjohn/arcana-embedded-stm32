#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace sdbench {

class SdBenchmarkService {
public:
    struct Output {
        Observable<SdBenchmarkModel>* StatsEvents;
    };

    Output output;

    virtual ~SdBenchmarkService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    SdBenchmarkService() : output() {
        output.StatsEvents = 0;
    }
};

} // namespace sdbench
} // namespace arcana
