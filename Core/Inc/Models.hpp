/**
 * @file Models.hpp
 * @brief Model definitions for Observable Pattern (C++ version)
 */

#ifndef ARCANA_MODELS_HPP
#define ARCANA_MODELS_HPP

#include "Observable.hpp"

namespace arcana {

/**
 * @brief Model types enumeration
 */
enum class ModelType : uint8_t {
    Base = 0,
    Timer,
    Counter,
    User
};

/**
 * @brief Timer model - published by TimerService
 */
class TimerModel : public Model {
public:
    uint32_t tickCount = 0;
    uint16_t periodMs = 0;

    TimerModel() : Model(static_cast<uint8_t>(ModelType::Timer)) {}

    void update(uint16_t period) {
        updateTimestamp();
        tickCount++;
        periodMs = period;
    }
};

/**
 * @brief Counter model - example derived model
 */
class CounterModel : public Model {
public:
    uint32_t count = 0;

    CounterModel() : Model(static_cast<uint8_t>(ModelType::Counter)) {}

    void increment() {
        updateTimestamp();
        count++;
    }
};

} // namespace arcana

#endif /* ARCANA_MODELS_HPP */
