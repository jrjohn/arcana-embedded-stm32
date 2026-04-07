/**
 * @file SystemClock.hpp (host test stub)
 *
 * The real SystemClock.hpp reads STM32 RTC->CNTH/CNTL registers and
 * uses the same-directory rule from F103_COMMON. This stub overrides
 * it via the mocks/ -I path so production code that does
 * `#include "SystemClock.hpp"` (from a different directory) finds this
 * version first.
 *
 * Test control: tests call SystemClock::getInstance().sync(epoch) to
 * inject a known epoch, then code-under-test calls now() / dateYYYYMMDD().
 */
#pragma once

#include <cstdint>

namespace arcana {

class SystemClock {
public:
    static SystemClock& getInstance() {
        static SystemClock sInstance;
        return sInstance;
    }

    void sync(uint32_t epochSec) {
        mEpoch  = epochSec;
        mSynced = true;
    }
    bool isSynced() const { return mSynced; }
    uint32_t now() const  { return mEpoch; }
    uint32_t localNow() const {
        return mEpoch + static_cast<uint32_t>(mTzOffsetSec);
    }

    void setTzOffset(int16_t minutes) {
        mTzOffsetMin = minutes;
        mTzOffsetSec = static_cast<int32_t>(minutes) * 60;
    }
    int16_t tzOffsetMin() const { return mTzOffsetMin; }

    static uint32_t startOfDay(uint32_t epoch) { return epoch - (epoch % 86400); }

    static void toHMS(uint32_t epoch, uint8_t& h, uint8_t& m, uint8_t& s) {
        uint32_t daySeconds = epoch % 86400;
        h = (uint8_t)(daySeconds / 3600);
        m = (uint8_t)((daySeconds % 3600) / 60);
        s = (uint8_t)(daySeconds % 60);
    }

    static uint32_t dateYYYYMMDD(uint32_t epoch) {
        /* Howard Hinnant civil date — same as production header */
        uint32_t z = epoch / 86400 + 719468;
        uint32_t era = z / 146097;
        uint32_t doe = z - era * 146097;
        uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        uint32_t y = yoe + era * 400;
        uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        uint32_t mp = (5 * doy + 2) / 153;
        uint8_t  d  = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
        uint8_t  m  = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
        if (m <= 2) y++;
        return (uint32_t)y * 10000 + (uint32_t)m * 100 + d;
    }

    static uint32_t dateToEpoch(uint32_t yyyymmdd) {
        uint16_t y = (uint16_t)(yyyymmdd / 10000);
        uint8_t m  = (uint8_t)((yyyymmdd / 100) % 100);
        uint8_t d  = (uint8_t)(yyyymmdd % 100);
        int32_t yr = (int32_t)y;
        if (m <= 2) yr--;
        int32_t era = yr / 400;
        uint32_t yoe = (uint32_t)(yr - era * 400);
        uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        int32_t days = (int32_t)(era * 146097 + (int32_t)doe) - 719468;
        return (uint32_t)days * 86400;
    }

    /* Test helper — reset between tests */
    void resetForTest() {
        mEpoch       = 0;
        mSynced      = false;
        mTzOffsetMin = 0;
        mTzOffsetSec = 0;
    }

private:
    SystemClock() = default;

    uint32_t mEpoch       = 0;
    bool     mSynced      = false;
    int16_t  mTzOffsetMin = 0;
    int32_t  mTzOffsetSec = 0;
};

} // namespace arcana
