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
    : mEcgTimer(0)
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
    // ECG sample queue (ATS task → render timer)
    mEcgQueue = xQueueCreateStatic(ECG_QUEUE_LEN, 1,
                                    mEcgQueueStorage, &mEcgQueueBuf);

    // Render timer (4ms = 250Hz) — View pulls from ViewModel
    mEcgTimer = xTimerCreateStatic("lcd", pdMS_TO_TICKS(4), pdTRUE,
                                    this, renderTimerCallback, &mEcgTimerBuf);
    xTimerStart(mEcgTimer, 0);
    return ServiceStatus::OK;
}

void LcdServiceImpl::stop() {
    if (mEcgTimer) xTimerStop(mEcgTimer, 0);
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
}

// ---------------------------------------------------------------------------
// ECG: thread-safe push from any task
// ---------------------------------------------------------------------------

void LcdServiceImpl::pushEcgSample(uint8_t y) {
    if (mEcgQueue) xQueueSend(mEcgQueue, &y, 0);
}

// ---------------------------------------------------------------------------
// Render timer (4ms) — View pulls from ViewModel, all LCD in ONE context
// ---------------------------------------------------------------------------

void LcdServiceImpl::renderTimerCallback(TimerHandle_t timer) {
    LcdServiceImpl* self = static_cast<LcdServiceImpl*>(pvTimerGetTimerID(timer));
    if (!self->mActiveView) return;
    if (xSemaphoreTake(self->mLcdMutex, 0) != pdTRUE) return;

    // 1. Process ECG queue → ViewModel → View
    uint8_t y;
    if (xQueueReceive(self->mEcgQueue, &y, 0) == pdTRUE) {
        const LcdOutput& out = self->mViewModel.output();
        uint8_t cursor = out.ecgCursor;
        uint8_t prevY = out.ecgPrevY;

        LcdInput in;
        in.type = LcdInput::EcgSample;
        in.ecg.y = y;
        self->mViewModel.onEvent(in);

        self->mActiveView->renderEcgColumn(self->mLcd, cursor, y, prevY);
    }

    // 2. Render dirty fields (stats, time, temp) — checked at 250Hz but only draws when dirty
    if (self->mViewModel.output().dirty) {
        self->mActiveView->render(self->mLcd, self->mViewModel.output(), self->mRendered);
        self->mViewModel.clearDirty();
    }

    xSemaphoreGive(self->mLcdMutex);
}

} // namespace lcd
} // namespace arcana
