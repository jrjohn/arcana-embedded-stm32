/**
 * @file CounterService.hpp
 * @brief Counter Service - demonstrates subscribing to observables (C++ version)
 */

#ifndef ARCANA_COUNTER_SERVICE_HPP
#define ARCANA_COUNTER_SERVICE_HPP

#include "Observable.hpp"
#include "Models.hpp"

namespace arcana {

/**
 * @brief Counter Service class
 *
 * Subscribes to TimerService and increments a counter on each tick.
 * Demonstrates the observer pattern.
 */
class CounterService {
private:
    uint32_t count_ = 0;

    /* Observer callback */
    static void onTimerEvent(TimerModel* model, void* context);

public:
    /**
     * @brief Initialize and subscribe to timer observable
     * @param timerObs Timer observable to subscribe to
     */
    void init(Observable<TimerModel>* timerObs);

    /**
     * @brief Get current count
     */
    uint32_t getCount() const { return count_; }

    /**
     * @brief Reset counter
     */
    void reset() { count_ = 0; }
};

/* Global instance */
extern CounterService counterService;

} // namespace arcana

#endif /* ARCANA_COUNTER_SERVICE_HPP */
