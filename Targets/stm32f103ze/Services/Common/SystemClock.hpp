#pragma once

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include <cstdint>

namespace arcana {

/**
 * System clock with STM32F103 RTC backing.
 *
 * Time sources (priority order):
 *  1. RTC counter (persists across reset with VBAT battery)
 *  2. NTP sync updates RTC counter
 *  3. Uptime fallback if RTC was never set (counter == 0)
 */
class SystemClock {
public:
    static SystemClock& getInstance() {
        static SystemClock sInstance;
        return sInstance;
    }

    /** Sync from NTP — writes epoch to RTC counter */
    void sync(uint32_t epochSec) {
        setRtcCounter(epochSec);
        mSynced = true;
    }

    /** Returns true if NTP has synced at least once (this boot or persisted via RTC) */
    bool isSynced() const {
        // If RTC has a meaningful value (> year 2020), consider it synced
        if (getRtcCounter() > 1577836800) return true;  // > 2020-01-01
        return mSynced;
    }

    /** Current UTC epoch seconds. Returns RTC counter, or 0 if never set. */
    uint32_t now() const {
        return getRtcCounter();
    }

    /** Current local epoch seconds (UTC + timezone offset). For LCD display only. */
    uint32_t localNow() const {
        return now() + mTzOffsetSec;
    }

    /** Set timezone offset in minutes (e.g. +480 = UTC+8, -300 = UTC-5) */
    void setTzOffset(int16_t minutes) {
        mTzOffsetMin = minutes;
        mTzOffsetSec = static_cast<int32_t>(minutes) * 60;
    }

    /** Get timezone offset in minutes */
    int16_t tzOffsetMin() const { return mTzOffsetMin; }

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
    SystemClock() : mSynced(false), mTzOffsetMin(0), mTzOffsetSec(0) {}

    bool mSynced;
    int16_t mTzOffsetMin;
    int32_t mTzOffsetSec;

    // -- RTC register helpers (STM32F103 specific) --

    static uint32_t getRtcCounter() {
        // Read twice to handle counter rollover between CNTH and CNTL
        uint16_t h1 = RTC->CNTH;
        uint16_t l  = RTC->CNTL;
        uint16_t h2 = RTC->CNTH;
        if (h1 != h2) { l = RTC->CNTL; h1 = h2; }
        return ((uint32_t)h1 << 16) | l;
    }

    static void setRtcCounter(uint32_t value) {
        rtcEnterConfig();
        RTC->CNTH = (uint16_t)(value >> 16);
        RTC->CNTL = (uint16_t)(value & 0xFFFF);
        rtcExitConfig();
    }

    static void rtcWaitSync() {
        RTC->CRL &= ~RTC_CRL_RSF;
        while (!(RTC->CRL & RTC_CRL_RSF)) {}
    }

    static void rtcEnterConfig() {
        while (!(RTC->CRL & RTC_CRL_RTOFF)) {}  // wait for last write to finish
        RTC->CRL |= RTC_CRL_CNF;                // enter config mode
    }

    static void rtcExitConfig() {
        RTC->CRL &= ~RTC_CRL_CNF;               // exit config mode
        while (!(RTC->CRL & RTC_CRL_RTOFF)) {}   // wait for write to complete
    }

    // -- Civil date algorithms (Howard Hinnant) --

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

