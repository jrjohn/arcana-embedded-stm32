#pragma once

#include "Observable.hpp"
#include "F103Models.hpp"
#include "ServiceTypes.hpp"
#include "ff.h"

namespace arcana {
namespace io { class IoService; }
namespace sdbench { class SdBenchmarkService; }
namespace atsstorage {

class AtsStorageService {
public:
    struct Input {
        Observable<SensorDataModel>* SensorData;
        io::IoService*               Io;       // KEY1 format request
        sdbench::SdBenchmarkService* SdBench;  // refresh SD info after format
    };

    struct Output {
        Observable<StorageStatsModel>* StatsEvents;
    };

    Input input;
    Output output;

    virtual ~AtsStorageService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

    // Query records for a given day (for LCD chart display)
    virtual uint16_t queryByDate(uint32_t dateYYYYMMDD,
                                 SensorDataModel* out, uint16_t maxCount) = 0;

    /** Upload support: pending .ats file descriptor */
    struct PendingFile {
        char name[16];      // "YYYYMMDD.ats"
        uint32_t size;      // file size in bytes
        uint32_t date;      // YYYYMMDD as uint32
    };
    static const uint8_t MAX_PENDING = 8;

    virtual bool isReady() const = 0;
    virtual void pauseRecording() = 0;
    virtual void resumeRecording() = 0;
    virtual uint8_t listPendingUploads(PendingFile* out, uint8_t maxCount) = 0;
    virtual void markUploaded(uint32_t dateYYYYMMDD) = 0;
    /** Shared 4KB read cache — for upload file streaming */
    virtual uint8_t* readCache() = 0;
    /** Shared FIL for sequential operations (compact at boot, upload at runtime) */
    virtual FIL& sharedFil() = 0;
    virtual bool saveTzConfig(int16_t offsetMin, uint8_t autoCheck) = 0;
    virtual bool loadCredentials(uint8_t* outBuf, uint16_t bufSize, uint16_t& outLen) = 0;
    virtual bool saveCredentials(const uint8_t* data, uint16_t len) = 0;

protected:
    AtsStorageService() : input(), output() {
        input.SensorData = 0;
        input.Io = 0;
        input.SdBench = 0;
        output.StatsEvents = 0;
    }
};

} // namespace atsstorage
} // namespace arcana
