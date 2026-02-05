/**
 * @file TimeDisplayService.cpp
 * @brief Time Display Service implementation
 */

#include "TimeDisplayService.hpp"

namespace arcana {

/* Global instance */
TimeDisplayService timeDisplayService;

void TimeDisplayService::onTimerEvent(TimerModel* model, void* context) {
    TimeDisplayService* self = static_cast<TimeDisplayService*>(context);
    if (self == nullptr || model == nullptr) return;

    /* Update timer period from model */
    self->timerPeriodMs_ = model->periodMs;

    /* Accumulate milliseconds */
    self->milliseconds_ += model->periodMs;

    /* Roll over to seconds */
    while (self->milliseconds_ >= 1000) {
        self->milliseconds_ -= 1000;
        self->totalSeconds_++;
    }

    /* Update display string */
    self->updateTimeString();
}

void TimeDisplayService::updateTimeString() {
    uint8_t hours = getHours();
    uint8_t minutes = getMinutes();
    uint8_t seconds = getSeconds();
    uint16_t ms = getMilliseconds();

    /* Format: HH:MM:SS.mmm */
    snprintf(timeBuffer_, TIME_BUFFER_SIZE, "%02u:%02u:%02u.%03u",
             hours, minutes, seconds, ms);
}

void TimeDisplayService::init(Observable<TimerModel>* timerObs) {
    reset();

    if (timerObs != nullptr) {
        timerObs->subscribe(onTimerEvent, this);
    }
}

void TimeDisplayService::reset() {
    totalSeconds_ = 0;
    milliseconds_ = 0;
    updateTimeString();
}

} // namespace arcana
