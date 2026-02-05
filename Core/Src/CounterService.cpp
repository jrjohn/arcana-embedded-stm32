/**
 * @file CounterService.cpp
 * @brief Counter Service implementation
 */

#include "CounterService.hpp"

namespace arcana {

/* Global instance */
CounterService counterService;

void CounterService::onTimerEvent(TimerModel* model, void* context) {
    (void)model;
    CounterService* self = static_cast<CounterService*>(context);
    if (self != nullptr) {
        self->count_++;
    }
}

void CounterService::init(Observable<TimerModel>* timerObs) {
    count_ = 0;
    if (timerObs != nullptr) {
        timerObs->subscribe(onTimerEvent, this);
    }
}

} // namespace arcana
