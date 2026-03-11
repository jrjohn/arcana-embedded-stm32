#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {

/**
 * Simulated high-frequency ADC data source for testing batch write.
 * Generates synthetic ADS1298-like data at configurable sample rates.
 */
class AdcSimulatorService {
public:
    struct Input {
        // No inputs needed - self-timed
    };

    struct Output {
        Observable<AdcDataModel>* AdcData;
    };

    Input input;
    Output output;

    static AdcSimulatorService& getInstance();

    ServiceStatus initHAL() { return ServiceStatus::OK; }
    ServiceStatus init() { return ServiceStatus::OK; }
    ServiceStatus start();
    void stop();

    /**
     * Configure simulation parameters.
     * @param sampleRateHz Sample rate in Hz (e.g., 1000 for 1kSPS)
     * @param channels Number of channels (1-8)
     * @param batchTrigger Enable batch trigger mode (notify every N samples)
     */
    void configure(uint16_t sampleRateHz, uint8_t channels, bool batchTrigger = true);

    /**
     * Get current simulation statistics.
     */
    struct Stats {
        uint32_t totalSamples;
        uint32_t samplesPerSec;
        uint16_t currentRateHz;
    };
    Stats getStats() const;

private:
    AdcSimulatorService();
    ~AdcSimulatorService();
    AdcSimulatorService(const AdcSimulatorService&) = delete;
    AdcSimulatorService& operator=(const AdcSimulatorService&) = delete;

    static void simulatorTask(void* param);
    void taskLoop();
    void generateSample();

    // Task
    static const uint16_t TASK_STACK_SIZE = 512;
    StaticTask_t mTaskBuffer;
    StackType_t mTaskStack[TASK_STACK_SIZE];
    TaskHandle_t mTaskHandle;
    bool mRunning;

    // Configuration
    uint16_t mSampleRateHz;
    uint8_t mChannels;
    bool mBatchTrigger;
    uint16_t mBatchSize;

    // Data generation
    AdcDataModel mCurrentBatch;
    uint32_t mSampleCounter;
    uint32_t mBatchCounter;

    // Observable
    Observable<AdcDataModel> mAdcDataObs;

    // Stats
    uint32_t mWindowStartTick;
    uint32_t mSamplesInWindow;
    uint32_t mLastSampleRate;
};

} // namespace arcana
