/**
 * @file TimeDisplayService.hpp
 * @brief Time Display Service - Observer sample that tracks and displays time
 */

#ifndef ARCANA_TIME_DISPLAY_SERVICE_HPP
#define ARCANA_TIME_DISPLAY_SERVICE_HPP

#include "Observable.hpp"
#include "Models.hpp"
#include <cstdio>

namespace arcana {

/**
 * @brief Time Display Service
 *
 * Subscribes to TimerService and tracks elapsed time.
 * Demonstrates Observer pattern usage.
 */
class TimeDisplayService {
public:
    /* Output: formatted time string */
    static constexpr size_t TIME_BUFFER_SIZE = 16;

private:
    uint32_t totalSeconds_ = 0;
    uint32_t milliseconds_ = 0;
    uint16_t timerPeriodMs_ = 100;
    char timeBuffer_[TIME_BUFFER_SIZE] = "00:00:00.000";

    /* Observer callback */
    static void onTimerEvent(TimerModel* model, void* context);

    /* Update time string */
    void updateTimeString();

public:
    /**
     * @brief Initialize and subscribe to timer observable
     * @param timerObs Timer observable to subscribe to
     */
    void init(Observable<TimerModel>* timerObs);

    /**
     * @brief Get formatted time string (HH:MM:SS.mmm)
     * @return Pointer to time string buffer
     */
    const char* getTimeString() const { return timeBuffer_; }

    /**
     * @brief Get total elapsed seconds
     */
    uint32_t getTotalSeconds() const { return totalSeconds_; }

    /**
     * @brief Get hours component
     */
    uint8_t getHours() const { return (totalSeconds_ / 3600) % 24; }

    /**
     * @brief Get minutes component
     */
    uint8_t getMinutes() const { return (totalSeconds_ / 60) % 60; }

    /**
     * @brief Get seconds component
     */
    uint8_t getSeconds() const { return totalSeconds_ % 60; }

    /**
     * @brief Get milliseconds component
     */
    uint16_t getMilliseconds() const { return milliseconds_; }

    /**
     * @brief Reset time to zero
     */
    void reset();
};

/* Global instance */
extern TimeDisplayService timeDisplayService;

} // namespace arcana

#endif /* ARCANA_TIME_DISPLAY_SERVICE_HPP */
