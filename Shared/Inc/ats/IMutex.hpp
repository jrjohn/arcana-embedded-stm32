/**
 * @file IMutex.hpp
 * @brief Platform abstraction for mutex
 *
 * Implementations: FreeRtosMutex (STM32/ESP32), PosixMutex (Linux).
 */

#ifndef ARCANA_ATS_IMUTEX_HPP
#define ARCANA_ATS_IMUTEX_HPP

#include <cstdint>

namespace arcana {
namespace ats {

/**
 * @brief Abstract mutex interface
 *
 * 0xFFFFFFFF = infinite wait (maps to portMAX_DELAY on FreeRTOS).
 */
class IMutex {
public:
    virtual ~IMutex() {}

    /** @brief Acquire mutex. Returns true on success */
    virtual bool lock(uint32_t timeoutMs = 0xFFFFFFFF) = 0;

    virtual void unlock() = 0;
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_ATS_IMUTEX_HPP */
