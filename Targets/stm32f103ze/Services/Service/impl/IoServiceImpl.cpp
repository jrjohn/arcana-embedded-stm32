#include "IoServiceImpl.hpp"
#include "Esp8266.hpp"
#include "DisplayStatus.hpp"
#include <cstdio>

namespace arcana {
namespace io {

IoServiceImpl::IoServiceImpl()
    : mUploadRequested(false)
    , mCancelRequested(false)
    , mFormatRequested(false)
    , mCancelArmed(false)
    , mCooldownUntil(0)
    , mKey2Seen(false)
    , mKey2Prev(false)
    , mKey1Hold(0)
    , mTaskHandle(0)
{}

IoServiceImpl& IoServiceImpl::getInstance() {
    static IoServiceImpl sInstance;
    return sInstance;
}

ServiceStatus IoServiceImpl::initHAL() {
    // GPIO already configured by SdCard/Sensor initHAL (PA0, PC13)
    return ServiceStatus::OK;
}

ServiceStatus IoServiceImpl::start() {
    mTaskHandle = xTaskCreateStatic(
        taskFunc, "io-key", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 1,
        mTaskStack, &mTaskBuffer);
    return mTaskHandle ? ServiceStatus::OK : ServiceStatus::Error;
}

void IoServiceImpl::armCancel() {
    mCancelArmed = true;
    mCancelRequested = false;
}

void IoServiceImpl::disarmCancel() {
    mCancelArmed = false;
    mCancelRequested = false;
    mUploadRequested = false;
    mKey2Prev = false;  // prevent stale rising edge from same press
    // Cooldown: ignore KEY2 for 5s after cancel
    mCooldownUntil = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
}

void IoServiceImpl::taskFunc(void* param) {
    auto* self = static_cast<IoServiceImpl*>(param);
    vTaskDelay(pdMS_TO_TICKS(2000));  // let boot settle (PC13 backup-domain)
    self->taskLoop();
    vTaskDelete(0);
}

void IoServiceImpl::taskLoop() {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms poll — responsive + low CPU

        // --- KEY2 (PC13, active-LOW) ---
        bool key2Now = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET);

        if (!key2Now) {
            mKey2Seen = true;  // seen released at least once
        }

        if (mKey2Seen && xTaskGetTickCount() >= mCooldownUntil) {
            // Rising edge: was pressed, now released
            if (!key2Now && mKey2Prev) {
                if (mCancelArmed) {
                    // During upload: cancel
                    mCancelRequested = true;
                    printf("[KEY2] cancel\r\n");
                } else if (!mUploadRequested) {
                    // Signal ESP8266 lock — MQTT will yield and run upload
                    mUploadRequested = true;
                    Esp8266::getInstance().requestAccessAsync(Esp8266::User::Upload);
                    printf("[KEY2] upload\r\n");
                    display::toast("Uploading...", 10000,
                                   (uint32_t)xTaskGetTickCount(),
                                   display::colors::WHITE, 0x07E0);
                }
            }
        }

        mKey2Prev = key2Now;

        // --- KEY1 (PA0, active-HIGH) — long press 2s = format ---
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {
            mKey1Hold++;
            if (mKey1Hold == 3) {  // show toast after 300ms (debounce)
                display::toast("Format?", 2000,
                               (uint32_t)xTaskGetTickCount(),
                               display::colors::WHITE, 0xFD20);
            }
            if (mKey1Hold >= 20) {  // 20 × 100ms = 2s
                mFormatRequested = true;
                mKey1Hold = 0;
                printf("[KEY1] format\r\n");
            }
        } else {
            if (mKey1Hold > 0) display::toastState().dismissTick = 0;
            mKey1Hold = 0;
        }
    }
}

} // namespace io
} // namespace arcana
