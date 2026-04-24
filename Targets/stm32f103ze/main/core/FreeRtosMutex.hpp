/**
 * @file FreeRtosMutex.hpp
 * @brief IMutex implementation wrapping FreeRTOS static mutex
 *
 * Header-only. Static allocation (no heap).
 * Call init() once before use.
 */

#ifndef ARCANA_FREERTOS_MUTEX_HPP
#define ARCANA_FREERTOS_MUTEX_HPP

#include "ats/IMutex.hpp"
#include "FreeRTOS.h"
#include "semphr.h"

namespace arcana {
namespace ats {

class FreeRtosMutex : public IMutex {
public:
    FreeRtosMutex() : mHandle(0) {}

    void init() {
        mHandle = xSemaphoreCreateMutexStatic(&mBuf);
    }

    bool lock(uint32_t timeoutMs = 0xFFFFFFFF) override {
        if (!mHandle) return false;
        TickType_t ticks = (timeoutMs == 0xFFFFFFFF)
            ? portMAX_DELAY
            : pdMS_TO_TICKS(timeoutMs);
        return xSemaphoreTake(mHandle, ticks) == pdTRUE;
    }

    void unlock() override {
        if (mHandle) xSemaphoreGive(mHandle);
    }

private:
    SemaphoreHandle_t mHandle;
    StaticSemaphore_t mBuf;
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_FREERTOS_MUTEX_HPP */
