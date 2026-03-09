#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace storage {

class StorageService {
public:
    struct Input {
        Observable<SensorDataModel>* SensorData;
    };

    struct Output {
        Observable<StorageStatsModel>* StatsEvents;
    };

    Input input;
    Output output;

    virtual ~StorageService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

    // Read back the latest N records (decrypted)
    // Returns actual number of records read
    virtual uint16_t readRecords(SensorDataModel* out, uint16_t maxCount) = 0;
    virtual uint32_t getRecordCount() = 0;

protected:
    StorageService() : input(), output() {
        input.SensorData = 0;
        output.StatsEvents = 0;
    }
};

} // namespace storage
} // namespace arcana
