#pragma once

#include "LcdService.hpp"
#include "Ili9341Lcd.hpp"
#include "LcdViewModel.hpp"
#include "LcdView.hpp"
#include "MainView.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

namespace arcana {
namespace lcd {

/**
 * LCD Service — MVVM + multi-view architecture.
 *
 * Data flow:
 *   Observable callbacks → LcdInput → ViewModel.onEvent() → Output
 *   Active LcdView reads Output and renders to LCD.
 *
 * Views can be switched at runtime (button/touch).
 * Currently: MainView (dashboard + ECG).
 */
class LcdServiceImpl : public LcdService {
public:
    static LcdService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    /** Push an ECG sample from any task (thread-safe via queue) */
    void pushEcgSample(uint8_t y);

    /** Switch to a different view */
    void setView(LcdView* view);

private:
    LcdServiceImpl();
    ~LcdServiceImpl();
    LcdServiceImpl(const LcdServiceImpl&);
    LcdServiceImpl& operator=(const LcdServiceImpl&);

    // Observable callbacks
    static void onSensorData(SensorDataModel* model, void* ctx);
    static void onLightData(LightDataModel* model, void* ctx);
    static void onStorageStats(StorageStatsModel* model, void* ctx);
    static void onSdBenchmark(SdBenchmarkModel* model, void* ctx);
    static void onBaseTimer(TimerModel* model, void* ctx);

    // Render task — wakes on xTaskNotify from Observable callbacks / ECG push
    static void renderTask(void* param);
    void processRender();
    static const uint16_t RENDER_TASK_STACK = 256;
    StaticTask_t mRenderTaskBuf;
    StackType_t mRenderTaskStack[RENDER_TASK_STACK];
    TaskHandle_t mRenderTaskHandle;

    // ECG sample queue
    static const uint8_t ECG_QUEUE_LEN = 16;
    QueueHandle_t mEcgQueue;
    StaticQueue_t mEcgQueueBuf;
    uint8_t mEcgQueueStorage[ECG_QUEUE_LEN];

    // MVVM
    LcdViewModel mViewModel;
    LcdOutput mRendered;

    // View management
    Ili9341Lcd mLcd;
    MainView mMainView;
    LcdView* mActiveView;

    // LCD access mutex (FSMC not safe for concurrent access)
    SemaphoreHandle_t mLcdMutex;
    StaticSemaphore_t mLcdMutexBuf;
};

} // namespace lcd
} // namespace arcana
