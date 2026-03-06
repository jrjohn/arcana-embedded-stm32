#include "TimerServiceImpl.hpp"
#include "stm32f1xx_hal.h"

namespace arcana {
namespace timer {

TimerServiceImpl::TimerServiceImpl()
    : mFastTimerObs("TimerSvc Fast")
    , mBaseTimerObs("TimerSvc Base")
    , mTimerHandle(0)
    , mTimerBuffer()
    , mTickCount(0)
    , mRunning(false)
{
    output.FastTimer = &mFastTimerObs;
    output.BaseTimer = &mBaseTimerObs;
}

TimerServiceImpl::~TimerServiceImpl() {
    stop();
}

TimerService& TimerServiceImpl::getInstance() {
    static TimerServiceImpl sInstance;
    return sInstance;
}

ServiceStatus TimerServiceImpl::initHAL() {
    return ServiceStatus::OK;
}

ServiceStatus TimerServiceImpl::init() {
    mFastModel.periodMs = FAST_INTERVAL_MS;
    mBaseModel.periodMs = BASE_INTERVAL_MS;
    return ServiceStatus::OK;
}

ServiceStatus TimerServiceImpl::start() {
    mTickCount = 0;
    mRunning = true;

    mTimerHandle = xTimerCreateStatic(
        "svc_timer",
        pdMS_TO_TICKS(FAST_INTERVAL_MS),
        pdTRUE,
        this,
        timerCallback,
        &mTimerBuffer
    );

    if (!mTimerHandle) {
        return ServiceStatus::Error;
    }

    if (xTimerStart(mTimerHandle, 0) != pdPASS) {
        return ServiceStatus::Error;
    }

    return ServiceStatus::OK;
}

void TimerServiceImpl::stop() {
    mRunning = false;
    if (mTimerHandle) {
        xTimerStop(mTimerHandle, 0);
    }
}

void TimerServiceImpl::timerCallback(TimerHandle_t xTimer) {
    TimerServiceImpl* self = static_cast<TimerServiceImpl*>(pvTimerGetTimerID(xTimer));
    if (!self->mRunning) return;

    self->mTickCount++;

    // Fast timer fires every tick
    self->mFastModel.tickCount = self->mTickCount;
    self->mFastModel.updateTimestamp();
    self->mFastTimerObs.publish(&self->mFastModel);

    // Base timer fires every N ticks
    if ((self->mTickCount % BASE_DIVIDER) == 0) {
        self->mBaseModel.tickCount = self->mTickCount / BASE_DIVIDER;
        self->mBaseModel.updateTimestamp();
        self->mBaseTimerObs.publish(&self->mBaseModel);
    }
}

} // namespace timer
} // namespace arcana
