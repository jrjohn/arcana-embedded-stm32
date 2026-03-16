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
    //lcdStatus("[WiFi] Reset ESP...");
    mEsp.reset();

    //lcdStatus("[WiFi] AT test...");
    if (!mEsp.sendCmd("AT", "OK", 2000)) {
        mEsp.reset();
        if (!mEsp.sendCmd("AT", "OK", 2000)) {
            //lcdStatus("[WiFi] ERR: no resp");
            return false;
        }
    }
    //lcdStatus("[WiFi] AT OK");

    return connectWifi();
}

// --- Connect without reset (for reconnect when ESP still responsive) ---

bool WifiServiceImpl::connect() {
    //lcdStatus("[WiFi] Reconnecting...");
    return connectWifi();
}

// --- WiFi AP connection ---

bool WifiServiceImpl::connectWifi() {
    mEsp.sendCmd("AT+CWDHCP_CUR=1,1", "OK", 500);

    //lcdStatus("CWMODE=1...");
    if (!mEsp.sendCmd("AT+CWMODE=1", "OK", 2500)) {
        if (!mEsp.responseContains("no change")) {
            //lcdStatus("ERR: CWMODE fail");
            //showResponse(mEsp);
            return false;
        }
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);
    //lcdStatus("CWJAP...");
    //lcdStatus2("");
    if (mEsp.sendCmd(cmd, "OK", 15000)) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        //lcdStatus("[WiFi] Connected");
        return true;
    }
    //lcdStatus("ERR: CWJAP");
    //showResponse(mEsp);
    return false;
}

// --- NTP time sync via raw UDP ---

bool WifiServiceImpl::syncNtp() {
    //lcdStatus("[NTP] UDP...");

    // Open UDP connection to NTP server
    if (!mEsp.sendCmd("AT+CIPSTART=\"UDP\",\"pool.ntp.org\",123", "OK", 5000)) {
        //lcdStatus("[NTP] UDP fail");
        //showResponse(mEsp);
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }

    // Prepare 48-byte NTP request
    uint8_t ntpReq[48];
    memset(ntpReq, 0, sizeof(ntpReq));
    ntpReq[0] = 0x1B;  // LI=0, Version=3, Mode=3 (client)

    // Send via AT+CIPSEND
    if (!mEsp.sendCmd("AT+CIPSEND=48", ">", 2000)) {
        mEsp.sendCmd("AT+CIPCLOSE", "OK", 1000);
        //lcdStatus("[NTP] SEND fail");
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }

    // Clear old +IPD data, then send NTP request
    mEsp.clearMqttMsg();
    mEsp.sendData(ntpReq, 48, 1000);
    if (!mEsp.waitFor("SEND OK", 5000)) {
        mEsp.sendCmd("AT+CIPCLOSE", "OK", 1000);
        //lcdStatus("[NTP] no SENDOK");
        vTaskDelay(pdMS_TO_TICKS(2000));
        return false;
    }

    // +IPD may arrive in the SAME frame as "SEND OK" — ISR only captures +IPD
    // at mRxBuf position 0, so check mRxBuf directly first.
    //lcdStatus("[NTP] Waiting...");
    bool ok = false;

    // Case 1: +IPD already in mRxBuf (same frame as SEND OK)
    if (mEsp.responseContains("+IPD")) {
        uint32_t epoch = parseNtpResponse(
            mEsp.getResponse(), mEsp.getResponseLen());
        ok = applyNtpEpoch(epoch);
    }

    // Case 2: +IPD not yet received — clear RX so next frame starts at pos 0
    if (!ok) {
        mEsp.clearRx();
        for (int i = 0; i < 30; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (mEsp.hasMqttMsg()) break;
            // Also re-check mRxBuf in case +IPD arrived but not at pos 0
            if (mEsp.responseContains("+IPD")) break;
        }

        if (mEsp.hasMqttMsg()) {
            uint32_t epoch = parseNtpResponse(
                mEsp.getMqttMsg(), mEsp.getMqttMsgLen());
            mEsp.clearMqttMsg();
            ok = applyNtpEpoch(epoch);
        } else if (mEsp.responseContains("+IPD")) {
            uint32_t epoch = parseNtpResponse(
                mEsp.getResponse(), mEsp.getResponseLen());
            ok = applyNtpEpoch(epoch);
        } else {
            //lcdStatus("[NTP] No resp");
        }
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
    mEsp.sendCmd("AT+CIPCLOSE", "OK", 1000);
    return ok;
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

// --- Parse raw UDP NTP response from buffer ---
// Searches for "+IPD,NN:" anywhere in buf (may be preceded by "SEND OK\r\n")
// NTP transmit timestamp at byte offset 40-43 (big-endian, seconds since 1900)

uint32_t WifiServiceImpl::parseNtpResponse(const char* buf, uint16_t len) {
    // Search for "+IPD," anywhere in buffer
    const char* ipd = 0;
    for (uint16_t i = 0; i + 5 <= len; i++) {
        if (memcmp(buf + i, "+IPD,", 5) == 0) {
            ipd = buf + i;
            break;
        }
    }
    if (!ipd) return 0;

    // Find ":" after "+IPD,<len>"
    uint16_t remaining = len - (uint16_t)(ipd - buf);
    const char* colon = 0;
    for (uint16_t i = 5; i < remaining && i < 16; i++) {
        if (ipd[i] == ':') {
            colon = ipd + i;
            break;
        }
    }
    if (!colon) return 0;

    const uint8_t* data = (const uint8_t*)(colon + 1);
    uint16_t available = len - (uint16_t)(colon + 1 - buf);
    if (available < 44) return 0;  // need at least bytes 0..43

    // Extract transmit timestamp (bytes 40-43, big-endian)
    uint32_t ntpTime = ((uint32_t)data[40] << 24) |
                       ((uint32_t)data[41] << 16) |
                       ((uint32_t)data[42] << 8)  |
                       ((uint32_t)data[43]);

    // NTP epoch = 1900-01-01, Unix epoch = 1970-01-01
    static const uint32_t NTP_UNIX_OFFSET = 2208988800UL;
    if (ntpTime < NTP_UNIX_OFFSET) return 0;

    return ntpTime - NTP_UNIX_OFFSET;
}

} // namespace wifi
} // namespace arcana
