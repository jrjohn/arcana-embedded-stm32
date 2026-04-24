#pragma once

#include "ITimerService.hpp"
#include "FreeRTOS.h"
#include "timers.h"

namespace arcana {
namespace timer {

class TimerServiceImpl : public TimerService {
public:
    static TimerService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

private:
    /* Test access — host gtest fixture drives the private timerCallback. */
    friend struct TimerServiceTestAccess;

    TimerServiceImpl();
    ~TimerServiceImpl();
    TimerServiceImpl(const TimerServiceImpl&);
    TimerServiceImpl& operator=(const TimerServiceImpl&);

    static void timerCallback(TimerHandle_t xTimer);

    static const uint32_t FAST_INTERVAL_MS = 100;
    static const uint32_t BASE_INTERVAL_MS = 1000;
    static const uint32_t BASE_DIVIDER = BASE_INTERVAL_MS / FAST_INTERVAL_MS;

    Observable<TimerModel> mFastTimerObs;
    Observable<TimerModel> mBaseTimerObs;
    TimerModel mFastModel;
    TimerModel mBaseModel;

    TimerHandle_t mTimerHandle;
    StaticTimer_t mTimerBuffer;
    uint32_t mTickCount;
    bool mRunning;
};

} // namespace timer
} // namespace arcana
