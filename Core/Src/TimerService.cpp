/**
 * @file TimerService.cpp
 * @brief Timer Service implementation
 */

#include "TimerService.hpp"

namespace arcana {

/* Global instance */
TimerService timerService;

void TimerService::timerCallback(TimerHandle_t xTimer) {
    // Get instance from timer ID
    TimerService* self = static_cast<TimerService*>(pvTimerGetTimerID(xTimer));
    if (self == nullptr) return;

    // Update model
    self->model_.update(self->periodMs_);

    // Publish to observers (async via dispatcher) - check queue space first
    if (ObservableDispatcher::hasQueueSpace()) {
        self->observable.publish(&self->model_);
    }
    // If queue full, skip this tick (error callback will be triggered by enqueue)
}

void TimerService::init(uint16_t periodMs) {
    periodMs_ = periodMs;
    model_.periodMs = periodMs;

    // Create static timer
    timerHandle_ = xTimerCreateStatic(
        "SvcTimer",
        pdMS_TO_TICKS(periodMs),
        pdTRUE,  // Auto-reload
        this,    // Timer ID = this pointer
        timerCallback,
        &timerBuffer_
    );
}

void TimerService::start() {
    if (timerHandle_ != nullptr) {
        xTimerStart(timerHandle_, 0);
    }
}

void TimerService::stop() {
    if (timerHandle_ != nullptr) {
        xTimerStop(timerHandle_, 0);
    }
}

} // namespace arcana
