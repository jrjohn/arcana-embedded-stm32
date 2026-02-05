/**
 * @file TimerService.hpp
 * @brief Timer Service - publishes periodic timer events (C++ version)
 */

#ifndef ARCANA_TIMER_SERVICE_HPP
#define ARCANA_TIMER_SERVICE_HPP

#include "Observable.hpp"
#include "Models.hpp"
#include "timers.h"

namespace arcana {

/**
 * @brief Timer Service class
 *
 * Publishes TimerModel events at configurable intervals.
 * Uses FreeRTOS software timer with static allocation.
 */
class TimerService {
public:
    /* Output observable */
    Observable<TimerModel> observable{"Timer"};

private:
    TimerModel model_;
    uint16_t periodMs_ = 100;

    StaticTimer_t timerBuffer_;
    TimerHandle_t timerHandle_ = nullptr;

    static void timerCallback(TimerHandle_t xTimer);

public:
    /**
     * @brief Initialize timer service
     * @param periodMs Timer period in milliseconds
     */
    void init(uint16_t periodMs = 100);

    /**
     * @brief Start the timer
     */
    void start();

    /**
     * @brief Stop the timer
     */
    void stop();

    /**
     * @brief Get current tick count
     */
    uint32_t getTickCount() const { return model_.tickCount; }
};

/* Global instance */
extern TimerService timerService;

} // namespace arcana

#endif /* ARCANA_TIMER_SERVICE_HPP */
