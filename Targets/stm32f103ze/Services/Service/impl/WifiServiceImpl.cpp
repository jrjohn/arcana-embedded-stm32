#include "WifiServiceImpl.hpp"
#include "Credentials.hpp"
#include "SystemClock.hpp"
#include <cstdio>
#include <cstring>

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

    // Retry AT up to 5 times
    bool atOk = false;
    for (int i = 0; i < 5; i++) {
        if (mEsp.sendCmd("AT", "OK", 2000)) { atOk = true; break; }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!atOk) {
        printf("[WiFi] AT no response\r\n");
        return false;
    }
    printf("[WiFi] AT OK\r\n");

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
            printf("[WiFi] CWMODE fail\r\n");
            return false;
        }
    }
    printf("[WiFi] CWMODE OK\r\n");

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);
    printf("[WiFi] Joining %s...\r\n", WIFI_SSID);

    // AT v2.2 may send "WIFI CONNECTED" + "WIFI GOT IP" before "OK"
    if (mEsp.sendCmd(cmd, "OK", 20000)) {
        printf("[WiFi] Connected!\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        return true;
    }

    // Check if connected despite no "OK" (AT v2.2 may respond differently)
    if (mEsp.responseContains("GOT IP")) {
        printf("[WiFi] Connected (GOT IP)!\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        return true;
    }

    printf("[WiFi] CWJAP fail: %.*s\r\n",
           mEsp.getResponseLen() > 60 ? 60 : mEsp.getResponseLen(),
           mEsp.getResponse());
    return false;
}

// --- NTP time sync via AT+CIPSNTPCFG (AT v2.2+) ---

bool WifiServiceImpl::syncNtp() {
    // Configure SNTP: enable, timezone UTC+8, NTP server
    if (!mEsp.sendCmd("AT+CIPSNTPCFG=1,8,\"pool.ntp.org\"", "OK", 3000)) {
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
    // Add UTC+8 timezone offset
    epoch += 8UL * 3600UL;
    SystemClock::getInstance().sync(epoch);
    char buf[24];
    uint8_t h, m, s;
    SystemClock::toHMS(epoch, h, m, s);
    snprintf(buf, sizeof(buf), "[NTP] %02u:%02u:%02u", h, m, s);
    //lcdStatus(buf);
    return true;
}

// parseNtpResponse removed — using AT+CIPSNTPCFG instead of raw UDP

} // namespace wifi
} // namespace arcana
