#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace atsstorage {

class AtsStorageService {
public:
    struct Input {
        Observable<SensorDataModel>* SensorData;
    };

    struct Output {
        Observable<StorageStatsModel>* StatsEvents;
    };

    Input input;
    Output output;

    virtual ~AtsStorageService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

    // Query records for a given day (for LCD chart display)
    virtual uint16_t queryByDate(uint32_t dateYYYYMMDD,
                                 SensorDataModel* out, uint16_t maxCount) = 0;

protected:
    AtsStorageService() : input(), output() {
        input.SensorData = 0;
        output.StatsEvents = 0;
    }
};

} // namespace atsstorage
} // namespace arcana
