#pragma once

#include "IoService.hpp"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <cstdint>

namespace arcana {
namespace io {

/**
 * GPIO key service — independent task polls KEY1/KEY2 every 100ms.
 * Always responsive regardless of ATS/MQTT/WiFi state.
 *
 * KEY1 (PA0, active-HIGH): long press 2s → format SD
 * KEY2 (PC13, active-LOW): press+release → upload, during upload → cancel
 */
class IoServiceImpl : public IoService {
public:
    static IoServiceImpl& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus start() override;

    bool isUploadRequested() const override { return mUploadRequested; }
    void clearUploadRequest() override      { mUploadRequested = false; }

    bool isCancelRequested() const override { return mCancelRequested; }
    void clearCancelRequest() override      { mCancelRequested = false; }

    bool isFormatRequested() const override { return mFormatRequested; }
    void clearFormatRequest() override      { mFormatRequested = false; }

    void armCancel() override;
    void disarmCancel() override;

private:
    /* Test access — host gtest fixture drives the private taskLoop body. */
    friend struct IoServiceTestAccess;

    IoServiceImpl();

    static void taskFunc(void* param);
    void taskLoop();

    volatile bool mUploadRequested;
    volatile bool mCancelRequested;
    volatile bool mFormatRequested;
    volatile bool mCancelArmed;
    uint32_t mCooldownUntil;  // tick — ignore KEY2 until this time

    // KEY2 state
    bool mKey2Seen;     // seen HIGH at least once
    bool mKey2Prev;     // previous reading (true=pressed)

    // KEY1 state
    uint8_t mKey1Hold;  // hold counter (×100ms)

    static const uint16_t TASK_STACK_SIZE = 128;  // KEY polling only, upload runs in MQTT task
    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
};

} // namespace io
} // namespace arcana
