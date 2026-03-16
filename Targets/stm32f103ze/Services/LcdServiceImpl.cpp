#include "LcdServiceImpl.hpp"
#include "SystemClock.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {
namespace lcd {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

LcdServiceImpl::LcdServiceImpl()
    : mRenderTaskHandle(0)
    , mEcgQueue(0)
    , mViewModel()
    , mRendered()
    , mLcd()
    , mMainView()
    , mActiveView(&mMainView)
    , mLcdMutex(0)
{
}

LcdServiceImpl::~LcdServiceImpl() {}

LcdService& LcdServiceImpl::getInstance() {
    static LcdServiceImpl sInstance;
    return sInstance;
}

ServiceStatus LcdServiceImpl::initHAL() {
    mLcd.initHAL();
    return ServiceStatus::OK;
}

ServiceStatus LcdServiceImpl::init() {
    mLcdMutex = xSemaphoreCreateMutexStatic(&mLcdMutexBuf);
    mActiveView->onEnter(mLcd);

    // Subscribe to data sources (Service role: feed ViewModel only)
    if (input.SensorData)   input.SensorData->subscribe(onSensorData, this);
    if (input.LightData)    input.LightData->subscribe(onLightData, this);
    if (input.StorageStats) input.StorageStats->subscribe(onStorageStats, this);
    if (input.SdBenchmark)  input.SdBenchmark->subscribe(onSdBenchmark, this);
    if (input.BaseTimer)    input.BaseTimer->subscribe(onBaseTimer, this);

    return ServiceStatus::OK;
}

ServiceStatus LcdServiceImpl::start() {
    // ECG sample queue (ATS task → render task)
    mEcgQueue = xQueueCreateStatic(ECG_QUEUE_LEN, 1,
                                    mEcgQueueStorage, &mEcgQueueBuf);

    // Render task — blocks on xTaskNotifyWait, wakes on Observable / ECG events
    mRenderTaskHandle = xTaskCreateStatic(
        renderTask, "LcdRndr", RENDER_TASK_STACK,
        this, tskIDLE_PRIORITY + 1, mRenderTaskStack, &mRenderTaskBuf);
    if (!mRenderTaskHandle) return ServiceStatus::Error;
    return ServiceStatus::OK;
}

void LcdServiceImpl::stop() {
    // Task will exit when mActiveView is nulled
    mActiveView = nullptr;
}

void LcdServiceImpl::setView(LcdView* view) {
    if (xSemaphoreTake(mLcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (mActiveView) mActiveView->onExit(mLcd);
        mActiveView = view;
        mRendered = LcdOutput();
        if (mActiveView) mActiveView->onEnter(mLcd);
        xSemaphoreGive(mLcdMutex);
    }
}

// ---------------------------------------------------------------------------
// Service: Observable callbacks → ViewModel only (NO View access)
// ---------------------------------------------------------------------------

void LcdServiceImpl::onSensorData(SensorDataModel* model, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    LcdInput in;
    in.type = LcdInput::SensorData;
    in.sensor.temperature = model->temperature;
    self->mViewModel.onEvent(in);
    if (self->mRenderTaskHandle) xTaskNotifyGive(self->mRenderTaskHandle);
}

void LcdServiceImpl::onLightData(LightDataModel*, void*) {}

void LcdServiceImpl::onStorageStats(StorageStatsModel* model, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    LcdInput in;
    in.type = LcdInput::StorageStats;
    in.storage.records = model->recordCount;
    in.storage.rate = model->writesPerSec;
    in.storage.totalKB = model->totalKB;
    in.storage.kbps = model->kbPerSec;
    self->mViewModel.onEvent(in);
    if (self->mRenderTaskHandle) xTaskNotifyGive(self->mRenderTaskHandle);
}

void LcdServiceImpl::onSdBenchmark(SdBenchmarkModel*, void*) {}

void LcdServiceImpl::onBaseTimer(TimerModel*, void* ctx) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(ctx);
    LcdInput in;
    in.type = LcdInput::TimerTick;
    in.timer.epoch = SystemClock::getInstance().now();
    in.timer.synced = SystemClock::getInstance().isSynced();
    in.timer.uptime = xTaskGetTickCount() / configTICK_RATE_HZ;
    self->mViewModel.onEvent(in);
    if (self->mRenderTaskHandle) xTaskNotifyGive(self->mRenderTaskHandle);
}

// ---------------------------------------------------------------------------
// ECG: thread-safe push from any task
// ---------------------------------------------------------------------------

void LcdServiceImpl::pushEcgSample(uint8_t y) {
    if (mEcgQueue) {
        xQueueSend(mEcgQueue, &y, 0);
        if (mRenderTaskHandle) xTaskNotifyGive(mRenderTaskHandle);
    }
}

// ---------------------------------------------------------------------------
// Render task — blocks on xTaskNotify, wakes on Observable / ECG events
// ---------------------------------------------------------------------------

void LcdServiceImpl::renderTask(void* param) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(param);
    for (;;) {
        // Block until notified, or timeout at 100ms (safety: 10Hz minimum render)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        self->processRender();
    }
}

void LcdServiceImpl::processRender() {
    if (!mActiveView) return;
    if (xSemaphoreTake(mLcdMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    // 1. Drain all pending ECG samples → ViewModel → View
    uint8_t y;
    while (xQueueReceive(mEcgQueue, &y, 0) == pdTRUE) {
        const LcdOutput& out = mViewModel.output();
        uint8_t cursor = out.ecgCursor;
        uint8_t prevY = out.ecgPrevY;

        LcdInput in;
        in.type = LcdInput::EcgSample;
        in.ecg.y = y;
        mViewModel.onEvent(in);

        mActiveView->renderEcgColumn(mLcd, cursor, y, prevY);
    }

    // 2. Render dirty fields (stats, time, temp)
    if (mViewModel.output().dirty) {
        mActiveView->render(mLcd, mViewModel.output(), mRendered);
        mViewModel.clearDirty();
    }

    xSemaphoreGive(mLcdMutex);
}

} // namespace lcd
} // namespace arcana
