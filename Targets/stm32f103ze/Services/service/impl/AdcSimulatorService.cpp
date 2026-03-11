#include "AdcSimulatorService.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <cstring>
#include <cmath>

// Wait for exFAT mount from SdBenchmarkService
extern "C" {
    extern volatile uint8_t g_exfat_ready;
}

namespace arcana {

AdcSimulatorService& AdcSimulatorService::getInstance() {
    static AdcSimulatorService sInstance;
    return sInstance;
}

AdcSimulatorService::AdcSimulatorService()
    : mTaskBuffer()
    , mTaskStack{}
    , mTaskHandle(0)
    , mRunning(false)
    , mSampleRateHz(100)  // Default to 100 SPS for safe testing
    , mChannels(8)
    , mBatchTrigger(true)
    , mBatchSize(10)
    , mCurrentBatch()
    , mSampleCounter(0)
    , mBatchCounter(0)
    , mAdcDataObs("AdcSimulator")
    , mWindowStartTick(0)
    , mSamplesInWindow(0)
    , mLastSampleRate(0)
{
    output.AdcData = &mAdcDataObs;
}

AdcSimulatorService::~AdcSimulatorService() {
    stop();
}

void AdcSimulatorService::configure(uint16_t sampleRateHz, uint8_t channels, bool batchTrigger) {
    mSampleRateHz = sampleRateHz;
    mChannels = (channels > 8) ? 8 : channels;
    mBatchTrigger = batchTrigger;
    // Batch size matches storage batch target (default 10)
    mBatchSize = 10;
}

ServiceStatus AdcSimulatorService::start() {
    mRunning = true;
    // Use lower priority (same as SdStorage) to avoid blocking
    mTaskHandle = xTaskCreateStatic(
        simulatorTask, "AdcSim", TASK_STACK_SIZE,
        this, tskIDLE_PRIORITY + 1, mTaskStack, &mTaskBuffer);
    if (!mTaskHandle) return ServiceStatus::Error;
    return ServiceStatus::OK;
}

void AdcSimulatorService::stop() {
    mRunning = false;
    if (mTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(50));
        mTaskHandle = 0;
    }
}

void AdcSimulatorService::simulatorTask(void* param) {
    AdcSimulatorService* self = static_cast<AdcSimulatorService*>(param);
    self->taskLoop();
    vTaskDelete(0);
}

void AdcSimulatorService::taskLoop() {
    // Wait for exFAT filesystem to be ready (same as SdStorage)
    int waitCount = 0;
    const int MAX_WAIT = 300;  // 30 seconds timeout
    while (!g_exfat_ready && mRunning && waitCount < MAX_WAIT) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waitCount++;
    }
    if (!mRunning) { vTaskDelete(0); return; }
    
    // Additional delay to let SdStorage fully initialize FlashDB
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Calculate tick interval from sample rate
    // For high rates, we generate multiple samples per tick
    const uint32_t TICK_MS = 100;  // 100ms task period (slower for safety)
    const uint32_t SAMPLES_PER_TICK = (mSampleRateHz * TICK_MS) / 1000;
    
    mWindowStartTick = xTaskGetTickCount();

    while (mRunning) {
        uint32_t startTick = xTaskGetTickCount();
        
        // Generate samples for this tick period
        for (uint32_t i = 0; i < SAMPLES_PER_TICK && mRunning; i++) {
            generateSample();
        }

        // Update stats every second
        uint32_t now = xTaskGetTickCount();
        if ((now - mWindowStartTick) >= pdMS_TO_TICKS(1000)) {
            mLastSampleRate = mSamplesInWindow;
            mSamplesInWindow = 0;
            mWindowStartTick = now;
        }

        // Publish samples if:
        // - Batch mode: buffer is full (mBatchSize samples)
        // - Single sample mode: we have at least 1 sample
        uint16_t triggerCount = mBatchTrigger ? mBatchSize : 1;
        if (mCurrentBatch.sampleCount >= triggerCount) {
            // Set timestamps
            mCurrentBatch.firstTimestamp = mSampleCounter - mCurrentBatch.sampleCount;
            mCurrentBatch.lastTimestamp = mSampleCounter;
            
            // Publish batch
            mAdcDataObs.publish(&mCurrentBatch);
            
            // Reset batch
            mCurrentBatch = AdcDataModel();
            mCurrentBatch.activeChannels = (1 << mChannels) - 1;
            mCurrentBatch.sampleRateHz = mSampleRateHz;
            mBatchCounter++;
        }

        // Delay to maintain timing
        uint32_t elapsed = xTaskGetTickCount() - startTick;
        if (elapsed < TICK_MS) {
            vTaskDelay(pdMS_TO_TICKS(TICK_MS - elapsed));
        }
    }

    // Flush any remaining samples
    if (mCurrentBatch.sampleCount > 0) {
        mAdcDataObs.publish(&mCurrentBatch);
    }
}

void AdcSimulatorService::generateSample() {
    if (mCurrentBatch.sampleCount >= AdcDataModel::MAX_BATCH_SAMPLES) {
        return;  // Buffer full, wait for flush
    }

    // Generate synthetic 24-bit ADC data
    // Pattern: sine wave on CH1, ramp on CH2, noise on others
    uint16_t sampleIdx = mCurrentBatch.sampleCount;
    uint16_t offset = sampleIdx * AdcDataModel::SAMPLE_SIZE;
    
    // Channel 1: Sine wave (simulated ECG-like signal)
    float angle = 2.0f * 3.14159f * (mSampleCounter % 100) / 100.0f;
    int32_t ch1 = (int32_t)(8388607 * sinf(angle));  // 24-bit signed
    
    // Channel 2: Slow ramp
    int32_t ch2 = (int32_t)(mSampleCounter % 16777216) - 8388608;
    
    // Other channels: Pseudo-random noise
    int32_t noise = (int32_t)(xTaskGetTickCount() * 12345) % 16777216 - 8388608;

    // Store in buffer (24-bit big-endian, ADS1298 format)
    for (uint8_t ch = 0; ch < mChannels && ch < 8; ch++) {
        int32_t value = 0;
        switch (ch) {
            case 0: value = ch1; break;
            case 1: value = ch2; break;
            default: value = noise + ch * 1000; break;
        }
        
        // Clamp to 24-bit signed
        if (value > 8388607) value = 8388607;
        if (value < -8388608) value = -8388608;
        
        // Store as 24-bit (3 bytes, big-endian)
        uint8_t* ptr = &mCurrentBatch.sampleBuffer[offset + ch * 3];
        ptr[0] = (value >> 16) & 0xFF;
        ptr[1] = (value >> 8) & 0xFF;
        ptr[2] = value & 0xFF;
    }

    mCurrentBatch.sampleCount++;
    mSampleCounter++;
    mSamplesInWindow++;
}

AdcSimulatorService::Stats AdcSimulatorService::getStats() const {
    Stats s;
    s.totalSamples = mSampleCounter;
    s.samplesPerSec = mLastSampleRate;
    s.currentRateHz = mSampleRateHz;
    return s;
}

} // namespace arcana
