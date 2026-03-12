#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace sdstorage {

class SdStorageService {
public:
    struct Input {
        Observable<SensorDataModel>* SensorData;
        Observable<AdcDataModel>* AdcData;      // High-frequency ADC input (ADS1298)
    };

    struct Output {
        Observable<StorageStatsModel>* StatsEvents;
    };

    Input input;
    Output output;

    virtual ~SdStorageService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

    /**
     * Configure batch write parameters for high-frequency ADC data.
     * @param samplesPerBatch Number of ADC samples to buffer before writing to FlashDB
     * @return true if configuration accepted
     */
    virtual bool configureBatchWrite(uint16_t samplesPerBatch) = 0;

    /**
     * Flush pending batch buffer immediately (for shutdown/sync).
     * @return true if successful
     */
    virtual bool flushBatch() = 0;

    // Export a day's records to YYYYMMDD.enc on SD card
    virtual bool exportDailyFile(uint32_t date) = 0;

    // Check if a date's .enc file has been uploaded (KVDB lookup)
    virtual bool isDateUploaded(uint32_t date) = 0;

    // Mark a date as uploaded in KVDB
    virtual bool markDateUploaded(uint32_t date) = 0;

    // Query records for a given day (for LCD chart display)
    virtual uint16_t queryByDate(uint32_t dateYYYYMMDD,
                                 SensorDataModel* out, uint16_t maxCount) = 0;

    // Query ADC data for a given time range (returns sample count)
    virtual uint32_t queryAdcByTimeRange(uint32_t startTime, uint32_t endTime,
                                         AdcDataModel* out, uint16_t maxBatches) = 0;

    /**
     * Enable internal stress test: generate dummy writes at the given rate,
     * independent of sensor data. Set hz=0 to disable.
     */
    virtual void enableStressTest(uint16_t hz) { (void)hz; }

    /**
     * Enable ADC batch stress test: simulate 8-channel ADC at given sample rate.
     * Data is batched into batchSize samples per FlashDB blob.
     * Set sps=0 to disable.
     */
    virtual void enableAdcStressTest(uint16_t sps, uint16_t batchSize) {
        (void)sps; (void)batchSize;
    }

protected:
    SdStorageService() : input(), output() {
        input.SensorData = 0;
        input.AdcData = 0;
        output.StatsEvents = 0;
    }
};

} // namespace sdstorage
} // namespace arcana
