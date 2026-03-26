#include "WifiServiceImpl.hpp"
#include "Credentials.hpp"
#include "SystemClock.hpp"
#include "Log.hpp"
#include "EventCodes.hpp"
#include <cstring>
#include <cstdio>

namespace arcana {
namespace wifi {

const char* WifiServiceImpl::WIFI_SSID = WIFI_SSID_VALUE;
const char* WifiServiceImpl::WIFI_PASS = WIFI_PASS_VALUE;

WifiServiceImpl::WifiServiceImpl()
    : mEsp(Esp8266::getInstance())
{}

WifiServiceImpl::~WifiServiceImpl() {}

WifiService& WifiServiceImpl::getInstance() {
    static WifiServiceImpl sInstance;
    return sInstance;
}

ServiceStatus WifiServiceImpl::initHAL() {
    if (!mEsp.initHAL()) {
        return ServiceStatus::Error;
    }
    return ServiceStatus::OK;
}

ServiceStatus WifiServiceImpl::init() {
    return ServiceStatus::OK;
}

ServiceStatus WifiServiceImpl::start() {
    return ServiceStatus::OK;
}

void WifiServiceImpl::stop() {}

Esp8266& WifiServiceImpl::getEsp() {
    return mEsp;
}

// --- Full reset + connect ---

bool WifiServiceImpl::resetAndConnect() {
    mEsp.reset();
    vTaskDelay(pdMS_TO_TICKS(1000));  // AT v2.2 FreeRTOS boot needs extra time

    // Try AT at 460800 (expected: AT+UART_DEF already saved)
    bool atOk = false;
    for (int i = 0; i < 3; i++) {
        if (mEsp.sendCmd("AT", "OK", 2000)) { atOk = true; break; }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (!atOk) {
        // Fallback: ESP8266 might be at 921600 (previous test) or 115200 (fresh)
        printf("[WiFi] 460800 fail, trying 921600\r\n");
        mEsp.setBaud(921600);
        vTaskDelay(pdMS_TO_TICKS(200));
        for (int i = 0; i < 3; i++) {
            if (mEsp.sendCmd("AT", "OK", 2000)) { atOk = true; break; }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (!atOk) {
            // Try 115200 (factory default)
            printf("[WiFi] 921600 fail, trying 115200\r\n");
            mEsp.setBaud(115200);
            vTaskDelay(pdMS_TO_TICKS(200));
            for (int i = 0; i < 3; i++) {
                if (mEsp.sendCmd("AT", "OK", 2000)) { atOk = true; break; }
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        if (!atOk) {
            LOG_W(ats::ErrorSource::Wifi, evt::WIFI_AT_NO_RESP);
            return false;
        }
        // Permanently save 460800 to ESP8266 flash
        printf("[WiFi] AT+UART_DEF=460800\r\n");
        mEsp.sendCmd("AT+UART_DEF=460800,8,1,0,0", "OK", 2000);
        // Switch both sides to 460800 now
        if (!mEsp.speedUp(460800)) {
            LOG_W(ats::ErrorSource::Wifi, evt::WIFI_AT_NO_RESP);
            return false;
        }
        LOG_I(ats::ErrorSource::Wifi, 0x0321);  // UART_DEF programmed
    }
    LOG_I(ats::ErrorSource::Wifi, evt::WIFI_AT_OK);

    return connectWifi();
}

// --- Connect without reset (for reconnect when ESP still responsive) ---

bool WifiServiceImpl::connect() {
    //lcdStatus("[WiFi] Reconnecting...");
    return connectWifi();
}

// --- WiFi AP connection ---

bool WifiServiceImpl::connectWifi() {
    mEsp.sendCmd("AT+CWDHCP=1,1", "OK", 500);

    if (!mEsp.sendCmd("AT+CWMODE=1", "OK", 2500)) {
        if (!mEsp.responseContains("no change")) {
            LOG_W(ats::ErrorSource::Wifi, evt::WIFI_CWMODE_FAIL);
            return false;
        }
    }
    LOG_I(ats::ErrorSource::Wifi, evt::WIFI_CWMODE_OK);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);
    LOG_I(ats::ErrorSource::Wifi, evt::WIFI_JOINING);

    // AT v2.2 may send "WIFI CONNECTED" + "WIFI GOT IP" before "OK"
    if (mEsp.sendCmd(cmd, "OK", 20000)) {
        LOG_I(ats::ErrorSource::Wifi, evt::WIFI_CONNECTED);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return true;
    }

    // Check if connected despite no "OK" (AT v2.2 may respond differently)
    if (mEsp.responseContains("GOT IP")) {
        LOG_I(ats::ErrorSource::Wifi, evt::WIFI_GOT_IP);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return true;
    }

    LOG_W(ats::ErrorSource::Wifi, evt::WIFI_CWJAP_FAIL);
    return false;
}

// --- NTP time sync via AT+CIPSNTPCFG (AT v2.2+) ---

bool WifiServiceImpl::syncNtp() {
    // Configure SNTP: enable, timezone UTC (offset applied in SystemClock::localNow)
    if (!mEsp.sendCmd("AT+CIPSNTPCFG=1,0,\"pool.ntp.org\"", "OK", 3000)) {
        return false;
    }

    // Wait for SNTP to sync (poll AT+CIPSNTPTIME? up to 10s)
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (int i = 0; i < 5; i++) {
        if (mEsp.sendCmd("AT+CIPSNTPTIME?", "OK", 3000)) {
            // Response: +CIPSNTPTIME:Mon Mar 18 13:45:00 2026
            // Parse year to check if valid (not 1970)
            const char* resp = mEsp.getResponse();
            if (resp && !strstr(resp, "1970") && strstr(resp, "202")) {
                // Got valid time — now query epoch via AT+SYSTIMESTAMP
                if (mEsp.sendCmd("AT+SYSTIMESTAMP?", "OK", 2000)) {
                    // Response: +SYSTIMESTAMP:<epoch_ms>
                    const char* ts = strstr(mEsp.getResponse(), "+SYSTIMESTAMP:");
                    if (ts) {
                        ts += 14;  // skip "+SYSTIMESTAMP:"
                        uint32_t epoch = 0;
                        while (*ts >= '0' && *ts <= '9') {
                            epoch = epoch * 10 + (*ts - '0');
                            ts++;
                            // epoch is in milliseconds for ESP32, seconds for ESP8266
                            // If value > 2000000000, it's milliseconds
                            if (epoch > 2000000000UL) {
                                epoch /= 1000;
                                break;
                            }
                        }
                        return applyNtpEpoch(epoch);
                    }
                }
                // Fallback: just mark as synced if we see valid date
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    return false;
}

bool WifiServiceImpl::applyNtpEpoch(uint32_t epoch) {
    if (epoch <= 1700000000) {
        if (epoch != 0) {
            char dbg[24];
            snprintf(dbg, sizeof(dbg), "[NTP] e=%lu", (unsigned long)epoch);
            //lcdStatus(dbg);
        }
        return false;
    }
    // Store pure UTC — timezone offset applied only in SystemClock::localNow()
    SystemClock::getInstance().sync(epoch);
    char buf[28];
    uint8_t h, m, s;
    SystemClock::toHMS(epoch, h, m, s);
    snprintf(buf, sizeof(buf), "[NTP] UTC %02u:%02u:%02u", h, m, s);
    //lcdStatus(buf);
    return true;
}

// parseNtpResponse removed — using AT+CIPSNTPCFG instead of raw UDP

// --- IP geolocation for timezone auto-detect ---

bool WifiServiceImpl::detectTimezone(int16_t& offsetMinutes) {
    // Open TCP to ip-api.com (free, no key, 45 req/min)
    if (!mEsp.sendCmd("AT+CIPSTART=\"TCP\",\"ip-api.com\",80", "OK", 5000)) {
        if (!mEsp.responseContains("ALREADY CONNECTED")) {
            return false;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // HTTP GET — fields=offset returns {"offset":<seconds>}
    static const char httpReq[] =
        "GET /json/?fields=offset HTTP/1.1\r\n"
        "Host: ip-api.com\r\n"
        "Connection: close\r\n\r\n";
    const uint16_t reqLen = sizeof(httpReq) - 1;

    char cipsend[24];
    snprintf(cipsend, sizeof(cipsend), "AT+CIPSEND=%u", reqLen);
    if (!mEsp.sendCmd(cipsend, ">", 3000)) {
        mEsp.sendCmd("AT+CIPCLOSE", "OK", 1000);
        return false;
    }

    mEsp.sendData(reinterpret_cast<const uint8_t*>(httpReq), reqLen, 3000);

    // Wait for response (ip-api typically responds in <500ms)
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (!mEsp.waitFor("\"offset\"", 5000)) {
        mEsp.sendCmd("AT+CIPCLOSE", "OK", 1000);
        return false;
    }

    // Parse: {"offset":28800} or {"offset":-18000}
    const char* resp = mEsp.getResponse();
    const char* p = strstr(resp, "\"offset\":");
    if (!p) {
        mEsp.sendCmd("AT+CIPCLOSE", "OK", 1000);
        return false;
    }
    p += 9;  // skip "offset":

    bool negative = false;
    if (*p == '-') { negative = true; p++; }

    int32_t offsetSec = 0;
    while (*p >= '0' && *p <= '9') {
        offsetSec = offsetSec * 10 + (*p - '0');
        p++;
    }
    if (negative) offsetSec = -offsetSec;

    mEsp.sendCmd("AT+CIPCLOSE", "OK", 1000);

    offsetMinutes = static_cast<int16_t>(offsetSec / 60);
    return true;
}

} // namespace wifi
} // namespace arcana
