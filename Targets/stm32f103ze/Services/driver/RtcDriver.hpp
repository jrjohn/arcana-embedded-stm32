#pragma once

#include "stm32f1xx_hal.h"
#include <cstdint>
#include <cstdio>

namespace arcana {

/**
 * Minimal RTC driver for STM32F103 using direct register access.
 *
 * Uses the 32.768KHz LSE crystal + CR1220 backup battery on the
 * 野火霸道 V2 board. The RTC counter stores Unix epoch seconds.
 * BKP_DR1 holds a magic value (0xA5A5) to detect if the RTC was
 * previously configured (survives power cycle with VBAT).
 *
 * Call init() once at boot (before FreeRTOS scheduler).
 * Call read() to get epoch seconds.
 * Call write() to set epoch (e.g. after NTP sync).
 */
class RtcDriver {
public:
    static RtcDriver& getInstance() {
        static RtcDriver sInstance;
        return sInstance;
    }

    /**
     * Initialize RTC with LSE clock source.
     * On first boot (no VBAT magic): configures LSE, prescaler, counter=0.
     * On warm boot (VBAT magic present): just waits for register sync.
     * Returns true if LSE started successfully.
     */
    bool init() {
        // Enable PWR and BKP clocks
        __HAL_RCC_PWR_CLK_ENABLE();
        __HAL_RCC_BKP_CLK_ENABLE();

        // Enable backup domain access
        PWR->CR |= PWR_CR_DBP;

        if (BKP->DR1 != MAGIC) {
            // First time: full RTC configuration
            printf("[RTC] First init, starting LSE...\n");

            // Reset backup domain to ensure clean LSE start
            RCC->BDCR |= RCC_BDCR_BDRST;
            RCC->BDCR &= ~RCC_BDCR_BDRST;

            // Enable LSE
            RCC->BDCR |= RCC_BDCR_LSEON;

            // Wait for LSE ready (timeout ~3 seconds)
            uint32_t timeout = 300000;
            while (!(RCC->BDCR & RCC_BDCR_LSERDY)) {
                if (--timeout == 0) {
                    printf("[RTC] LSE timeout!\n");
                    return false;
                }
            }

            // Select LSE as RTC clock source
            RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_0;

            // Enable RTC clock
            RCC->BDCR |= RCC_BDCR_RTCEN;

            // Wait for RTC registers synchronized
            waitSync();

            // Configure prescaler for 1Hz (32768 - 1 = 32767)
            waitWriteComplete();
            RTC->CRL |= RTC_CRL_CNF;
            RTC->PRLH = 0;
            RTC->PRLL = 32767;
            RTC->CNTH = 0;
            RTC->CNTL = 0;
            RTC->CRL &= ~RTC_CRL_CNF;
            waitWriteComplete();

            // Store magic in backup register
            BKP->DR1 = MAGIC;
            printf("[RTC] Configured (LSE 32.768KHz)\n");
        } else {
            // RTC already configured from previous boot
            // Just ensure RTC clock is enabled and wait for sync
            if (!(RCC->BDCR & RCC_BDCR_RTCEN)) {
                RCC->BDCR |= RCC_BDCR_RTCEN;
            }
            waitSync();

            uint32_t epoch = read();
            printf("[RTC] Warm boot, counter=%lu\n", (unsigned long)epoch);
        }

        mInitOk = true;
        return true;
    }

    /** Read 32-bit RTC counter (epoch seconds). */
    uint32_t read() const {
        // Read high-low-high to handle rollover
        uint16_t h1 = RTC->CNTH;
        uint16_t lo = RTC->CNTL;
        uint16_t h2 = RTC->CNTH;
        if (h1 != h2) {
            lo = RTC->CNTL;
            h1 = h2;
        }
        return ((uint32_t)h1 << 16) | lo;
    }

    /** Write 32-bit RTC counter (epoch seconds). */
    void write(uint32_t epoch) {
        // Enable backup domain access (may have been disabled)
        PWR->CR |= PWR_CR_DBP;

        waitWriteComplete();
        RTC->CRL |= RTC_CRL_CNF;
        RTC->CNTH = (uint16_t)(epoch >> 16);
        RTC->CNTL = (uint16_t)(epoch & 0xFFFF);
        RTC->CRL &= ~RTC_CRL_CNF;
        waitWriteComplete();

        printf("[RTC] Set counter=%lu\n", (unsigned long)epoch);
    }

    bool isInitOk() const { return mInitOk; }

    /** Minimum valid epoch (~2024-01-01). Counter below this means RTC was never set. */
    static bool isValidEpoch(uint32_t epoch) {
        return epoch > 1704067200UL;  // 2024-01-01 00:00:00 UTC
    }

private:
    RtcDriver() : mInitOk(false) {}

    void waitSync() {
        RTC->CRL &= ~RTC_CRL_RSF;
        while (!(RTC->CRL & RTC_CRL_RSF)) {}
    }

    void waitWriteComplete() {
        while (!(RTC->CRL & RTC_CRL_RTOFF)) {}
    }

    bool mInitOk;

    static const uint16_t MAGIC = 0xA5A5;
};

} // namespace arcana
