#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace lcd {

// Forward declarations — View layer
class LcdViewModel;
class LcdView;

class LcdService {
public:
    struct Input {
        // Data sources (Observable)
        Observable<SensorDataModel>*    SensorData;
        Observable<LightDataModel>*     LightData;
        Observable<StorageStatsModel>*  StorageStats;
        Observable<SdBenchmarkModel>*   SdBenchmark;
        Observable<TimerModel>*         BaseTimer;

        // MVVM wiring (set by Controller::wireViews)
        LcdViewModel*                   ViewModel;
        LcdView*                        View;
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
        input.LightData = 0;
        input.StorageStats = 0;
        input.SdBenchmark = 0;
        input.BaseTimer = 0;
        input.ViewModel = 0;
        input.View = 0;
    }
};

} // namespace lcd
} // namespace arcana
