#pragma once

#include "FreeRTOS.h"
#include "task.h"
#include <cstdint>

namespace arcana {

class SystemClock {
public:
    static SystemClock& getInstance() {
        static SystemClock sInstance;
        return sInstance;
    }

    void sync(uint32_t epochSec) {
        mEpochAtSync = epochSec;
        mTickAtSync = xTaskGetTickCount();
        mSynced = true;
    }

    bool isSynced() const { return mSynced; }

    uint32_t now() const {
        if (!mSynced) return 0;
        uint32_t elapsedSec = (xTaskGetTickCount() - mTickAtSync) / configTICK_RATE_HZ;
        return mEpochAtSync + elapsedSec;
    }

    static uint32_t startOfDay(uint32_t epoch) {
        return epoch - (epoch % 86400);
    }

    static void toHMS(uint32_t epoch, uint8_t& h, uint8_t& m, uint8_t& s) {
        uint32_t daySeconds = epoch % 86400;
        h = (uint8_t)(daySeconds / 3600);
        m = (uint8_t)((daySeconds % 3600) / 60);
        s = (uint8_t)(daySeconds % 60);
    }

    static uint32_t dateYYYYMMDD(uint32_t epoch) {
        uint16_t y;
        uint8_t mon, d;
        epochToDate(epoch, y, mon, d);
        return (uint32_t)y * 10000 + (uint32_t)mon * 100 + d;
    }

    static uint32_t dateToEpoch(uint32_t yyyymmdd) {
        uint16_t y = (uint16_t)(yyyymmdd / 10000);
        uint8_t m  = (uint8_t)((yyyymmdd / 100) % 100);
        uint8_t d  = (uint8_t)(yyyymmdd % 100);
        return civilToEpoch(y, m, d);
    }

private:
    SystemClock() : mEpochAtSync(0), mTickAtSync(0), mSynced(false) {}

    uint32_t mEpochAtSync;
    TickType_t mTickAtSync;
    bool mSynced;

    // Civil date algorithms (no <time.h> dependency)
    // Based on Howard Hinnant's algorithms: http://howardhinnant.github.io/date_algorithms.html

    static void epochToDate(uint32_t epoch, uint16_t& year, uint8_t& month, uint8_t& day) {
        uint32_t z = epoch / 86400 + 719468;
        uint32_t era = z / 146097;
        uint32_t doe = z - era * 146097;
        uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        uint32_t y = yoe + era * 400;
        uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        uint32_t mp = (5 * doy + 2) / 153;
        day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
        month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
        if (month <= 2) y++;
        year = (uint16_t)y;
    }

    static uint32_t civilToEpoch(uint16_t y, uint8_t m, uint8_t d) {
        int32_t yr = (int32_t)y;
        if (m <= 2) yr--;
        int32_t era = yr / 400;
        uint32_t yoe = (uint32_t)(yr - era * 400);
        uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        int32_t days = (int32_t)(era * 146097 + (int32_t)doe) - 719468;
        return (uint32_t)days * 86400;
    }
};

} // namespace arcana
