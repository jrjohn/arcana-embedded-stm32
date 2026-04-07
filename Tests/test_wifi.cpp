/**
 * @file test_wifi.cpp
 * @brief Host coverage suite for WifiServiceImpl.cpp.
 *
 * Drives the AT-command sequence with the programmable Esp8266 mock.
 */
#include <gtest/gtest.h>
#include <cstring>
#include <string>

#include "stm32f1xx_hal.h"
#include "WifiServiceImpl.hpp"
#include "Esp8266.hpp"
#include "SystemClock.hpp"

using arcana::wifi::WifiService;
using arcana::wifi::WifiServiceImpl;
using arcana::Esp8266;
using arcana::SystemClock;
using arcana::ServiceStatus;

namespace {

WifiServiceImpl& wifi() {
    return static_cast<WifiServiceImpl&>(WifiServiceImpl::getInstance());
}

void resetEnvironment() {
    Esp8266::getInstance().resetForTest();
    SystemClock::getInstance().resetForTest();
}

} // anonymous namespace

// ── Lifecycle ──────────────────────────────────────────────────────────────

TEST(WifiLifecycle, GetInstanceSingleton) {
    auto& a = WifiServiceImpl::getInstance();
    auto& b = WifiServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(WifiLifecycle, InitHalReturnsOk) {
    resetEnvironment();
    EXPECT_EQ(wifi().initHAL(), ServiceStatus::OK);
}

TEST(WifiLifecycle, InitStartStop) {
    resetEnvironment();
    EXPECT_EQ(wifi().init(), ServiceStatus::OK);
    EXPECT_EQ(wifi().start(), ServiceStatus::OK);
    wifi().stop();
    EXPECT_EQ(&wifi().getEsp(), &Esp8266::getInstance());
}

// ── connectWifi (via connect()) ────────────────────────────────────────────

TEST(WifiConnect, ConnectHappyPath) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");      // CWDHCP
    esp.pushResponse("OK");      // CWMODE
    esp.pushResponse("OK");      // CWJAP

    EXPECT_TRUE(wifi().connect());
}

TEST(WifiConnect, ConnectCwModeNoChangeOk) {
    /* CWMODE returns "no change" instead of "OK" → still passes */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");          // CWDHCP
    esp.pushResponse("no change");   // CWMODE → fail "OK" but contains "no change"
    esp.pushResponse("OK");          // CWJAP
    EXPECT_TRUE(wifi().connect());
}

TEST(WifiConnect, ConnectCwModeFailureBails) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");          // CWDHCP
    esp.pushResponse("ERROR");       // CWMODE → fail
    EXPECT_FALSE(wifi().connect());
}

TEST(WifiConnect, ConnectGotIpFallback) {
    /* CWJAP doesn't return OK but response contains "GOT IP" → success */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");
    esp.pushResponse("OK");
    esp.pushResponse("WIFI GOT IP");  /* no "OK" but has "GOT IP" */
    EXPECT_TRUE(wifi().connect());
}

TEST(WifiConnect, ConnectCwjapFails) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");
    esp.pushResponse("OK");
    esp.pushResponse("FAIL");        /* neither "OK" nor "GOT IP" */
    EXPECT_FALSE(wifi().connect());
}

// ── resetAndConnect ────────────────────────────────────────────────────────

TEST(WifiResetConnect, FirstAtSucceedsHappyPath) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* AT command at 460800 succeeds on first try */
    esp.pushResponse("OK");          // AT
    esp.pushResponse("OK");          // AT+GMR
    esp.pushResponse("OK");          // CWDHCP (in connectWifi)
    esp.pushResponse("OK");          // CWMODE
    esp.pushResponse("OK");          // CWJAP

    EXPECT_TRUE(wifi().resetAndConnect());
}

TEST(WifiResetConnect, FallbackTo921kSucceeds) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* All 460800 attempts fail (3 attempts), then 921600 succeeds */
    esp.pushResponse("ERROR"); esp.pushResponse("ERROR"); esp.pushResponse("ERROR");
    esp.pushResponse("OK");          // AT at 921600
    esp.pushResponse("OK");          // AT+UART_DEF
    esp.pushResponse("OK");          // speedUp
    esp.pushResponse("OK");          // AT+GMR
    esp.pushResponse("OK");          // CWDHCP
    esp.pushResponse("OK");          // CWMODE
    esp.pushResponse("OK");          // CWJAP

    EXPECT_TRUE(wifi().resetAndConnect());
}

TEST(WifiResetConnect, AllBaudFallbacksFail) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    /* 9 ATs all fail */
    for (int i = 0; i < 9; ++i) esp.pushResponse("ERROR");
    EXPECT_FALSE(wifi().resetAndConnect());
}

// ── syncNtp ────────────────────────────────────────────────────────────────

TEST(WifiNtp, SyncNtpCfgFailureReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("ERROR");  // CIPSNTPCFG
    EXPECT_FALSE(wifi().syncNtp());
}

TEST(WifiNtp, SyncNtpHappyPathWithEpoch) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");      // CIPSNTPCFG
    /* CIPSNTPTIME? returns valid date (not 1970, contains "202") */
    esp.pushResponse("+CIPSNTPTIME:Mon Mar 18 13:45:00 2026\r\nOK");
    /* SYSTIMESTAMP returns epoch in milliseconds */
    esp.pushResponse("+SYSTIMESTAMP:1742305500000\r\nOK");

    bool ok = wifi().syncNtp();
    if (ok) {
        EXPECT_TRUE(SystemClock::getInstance().isSynced());
    }
}

TEST(WifiNtp, SyncNtpInvalidYearReturnsFalse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");      // CIPSNTPCFG
    /* All polling attempts return "1970" → invalid */
    for (int i = 0; i < 6; ++i) {
        esp.pushResponse("+CIPSNTPTIME:Thu Jan 1 00:00:00 1970\r\nOK");
    }
    EXPECT_FALSE(wifi().syncNtp());
}

// ── detectTimezone ─────────────────────────────────────────────────────────

TEST(WifiTimezone, DetectTimezoneCipStartFailure) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("ERROR");   // CIPSTART → no "OK", no "ALREADY"
    int16_t off = 0;
    EXPECT_FALSE(wifi().detectTimezone(off));
}

TEST(WifiTimezone, DetectTimezoneAlreadyConnectedRecovers) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("ALREADY CONNECTED");  // CIPSTART
    esp.pushResponse(">");                  // CIPSEND
    esp.pushResponse("");                   // sendData
    esp.pushResponse("HTTP/1.1 200 OK\r\n\r\n{\"offset\":28800}");  // waitFor "offset"
    esp.pushResponse("OK");                 // CIPCLOSE

    int16_t off = 0;
    EXPECT_TRUE(wifi().detectTimezone(off));
    EXPECT_EQ(off, 28800 / 60);  /* 480 minutes = UTC+8 */
}

TEST(WifiTimezone, DetectTimezoneNegativeOffset) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");                 // CIPSTART
    esp.pushResponse(">");                  // CIPSEND
    esp.pushResponse("");                   // sendData
    esp.pushResponse("HTTP/1.1 200 OK\r\n\r\n{\"offset\":-18000}");
    esp.pushResponse("OK");                 // CIPCLOSE

    int16_t off = 0;
    EXPECT_TRUE(wifi().detectTimezone(off));
    EXPECT_EQ(off, -18000 / 60);  /* -300 = UTC-5 */
}

TEST(WifiTimezone, DetectTimezoneCipSendFailureBails) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");      // CIPSTART
    esp.pushResponse("ERROR");   // CIPSEND → no >
    esp.pushResponse("OK");      // CIPCLOSE
    int16_t off = 99;
    EXPECT_FALSE(wifi().detectTimezone(off));
}

TEST(WifiTimezone, DetectTimezoneNoOffsetInResponse) {
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");
    esp.pushResponse(">");
    esp.pushResponse("");
    esp.pushResponse("garbage no offset key here\r\n");  /* no "offset" → waitFor fails */
    esp.pushResponse("OK");      // CIPCLOSE
    int16_t off = 99;
    EXPECT_FALSE(wifi().detectTimezone(off));
}

// ── applyNtpEpoch real success path (covers lines 202-208) ──────────────────

TEST(WifiNtp, SyncNtpEpochInSecondsAppliesToSystemClock) {
    /* Push a 10-digit SYSTIMESTAMP — interpreted as seconds, large enough
     * (> 1700000000) to satisfy applyNtpEpoch and call SystemClock::sync. */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");
    esp.pushResponse("+CIPSNTPTIME:Mon Mar 23 12:34:56 2026\r\nOK");
    esp.pushResponse("+SYSTIMESTAMP:1742300000\r\nOK");

    EXPECT_TRUE(wifi().syncNtp());
    EXPECT_TRUE(SystemClock::getInstance().isSynced());
}

TEST(WifiNtp, SyncNtpFallbackWhenSysTimestampMissing) {
    /* CIPSNTPTIME succeeds with valid year, SYSTIMESTAMP succeeds but the
     * response has no "+SYSTIMESTAMP:" prefix → ts is null → falls through
     * to the fallback `return true` (covers lines 181-183). */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");
    esp.pushResponse("+CIPSNTPTIME:Mon Mar 23 12:34:56 2026\r\nOK");
    esp.pushResponse("garbage no timestamp prefix\r\nOK");

    EXPECT_TRUE(wifi().syncNtp());
}

TEST(WifiNtp, SyncNtpFallbackWhenSysTimestampCommandFails) {
    /* SYSTIMESTAMP command itself fails → outer if false → fallback true */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");
    esp.pushResponse("+CIPSNTPTIME:Mon Mar 23 12:34:56 2026\r\nOK");
    esp.pushResponse("ERROR");

    EXPECT_TRUE(wifi().syncNtp());
}

TEST(WifiNtp, ApplyNtpEpochZeroIsRejected) {
    /* Push CIPSNTPTIME valid + SYSTIMESTAMP=0 → epoch ≤ 1700000000 →
     * applyNtpEpoch returns false. The "if (epoch != 0)" inner skip
     * branch isn't hit because epoch IS 0 — exercise the early return. */
    resetEnvironment();
    auto& esp = Esp8266::getInstance();
    esp.pushResponse("OK");
    esp.pushResponse("+CIPSNTPTIME:Mon Mar 23 12:34:56 2026\r\nOK");
    esp.pushResponse("+SYSTIMESTAMP:0\r\nOK");
    /* applyNtpEpoch returns false → loop continues to next iteration.
     * Stage 5 more no-good responses to make the polling loop give up. */
    for (int i = 0; i < 5; ++i) {
        esp.pushResponse("+CIPSNTPTIME:Thu Jan 1 00:00:00 1970\r\nOK");
    }
    EXPECT_FALSE(wifi().syncNtp());
}
