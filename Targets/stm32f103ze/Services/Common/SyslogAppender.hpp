/**
 * @file SyslogAppender.hpp
 * @brief Log appender — UDP syslog via ESP8266 AT commands
 *
 * WARN+ events formatted as RFC 3164 syslog into a ring buffer.
 * MQTT task calls flushViaUdp() to send pending messages.
 * SPSC: ATS task produces (append), MQTT task consumes (flush).
 */

#pragma once

#include "Log.hpp"
#include "Esp8266.hpp"
#include <cstdio>
#include <cstring>

namespace arcana {
namespace log {

class SyslogAppender : public IAppender {
public:
    static SyslogAppender& getInstance() {
        static SyslogAppender sInstance;
        return sInstance;
    }

    void append(const LogEvent& event) override {
        static const char LEVEL_CH[] = "TDIWEF";
        static const char* const SRC_TAG[] = {
            "SYS",  "SDIO", "SENS", "WiFi", "Pump",
            "Cryp", "ATS",  "NTP",  "MQTT", "BLE",
            "OTA",  "CMD",  "LCD",
        };
        static const uint8_t SRC_COUNT =
            sizeof(SRC_TAG) / sizeof(SRC_TAG[0]);

        uint8_t next = (mHead + 1) & RING_MASK;
        if (next == mTail) return;  // drop on overflow

        // RFC 3164: <PRI>TAG: message
        // PRI = facility(1=user) * 8 + severity
        uint8_t sev = 6;  // info
        if (event.level >= static_cast<uint8_t>(Level::Fatal)) sev = 2;      // critical
        else if (event.level >= static_cast<uint8_t>(Level::Error)) sev = 3;  // error
        else if (event.level >= static_cast<uint8_t>(Level::Warn))  sev = 4;  // warning
        uint8_t pri = 8 + sev;  // facility=1 (user)

        char lvl = (event.level < sizeof(LEVEL_CH) - 1)
                   ? LEVEL_CH[event.level] : '?';
        const char* src = (event.source < SRC_COUNT)
                         ? SRC_TAG[event.source] : "???";

        // Syslog: event code only — no param detail (prevents info leak over UDP)
        snprintf(mRing[mHead], MSG_MAX_LEN,
                 "<%u>arcana: [%c][%s] 0x%04X",
                 (unsigned)pri, lvl, src, (unsigned)event.code);

        mHead = next;
    }

    Level minLevel() const override { return Level::Warn; }

    /** Open persistent UDP socket to syslog server. Call from MQTT task. */
    bool openUdp(Esp8266& esp) {
        if (mUdpOpen) return true;
        char cmd[64];
        snprintf(cmd, sizeof(cmd),
                 "AT+CIPSTART=\"UDP\",\"%s\",%u", SYSLOG_HOST, SYSLOG_PORT);
        if (esp.sendCmd(cmd, "OK", 3000)) {
            mUdpOpen = true;
            return true;
        }
        return false;
    }

    /** Close UDP socket. Call from MQTT task. */
    void closeUdp(Esp8266& esp) {
        if (!mUdpOpen) return;
        esp.sendCmd("AT+CIPCLOSE", "OK", 2000);
        mUdpOpen = false;
    }

    /**
     * Send all pending syslog messages via UDP. Call from MQTT task.
     * Returns number of messages sent.
     */
    uint8_t flushViaUdp(Esp8266& esp) {
        if (!mUdpOpen || mHead == mTail) return 0;

        uint8_t sent = 0;
        while (mHead != mTail && sent < 4) {  // max 4 per flush cycle
            uint16_t len = (uint16_t)strlen(mRing[mTail]);
            if (len == 0) { mTail = (mTail + 1) & RING_MASK; continue; }

            char cmd[24];
            snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)len);
            if (!esp.sendCmd(cmd, ">", 1000)) {
                // UDP send failed — connection may be broken
                mUdpOpen = false;
                break;
            }
            esp.sendData(reinterpret_cast<const uint8_t*>(mRing[mTail]),
                         len, 1000);
            if (!esp.waitFor("SEND OK", 2000)) {
                mUdpOpen = false;
                break;
            }

            mTail = (mTail + 1) & RING_MASK;
            sent++;
        }
        return sent;
    }

    /**
     * Enqueue a periodic heartbeat (bypasses Logger level filter).
     * Call from ATS taskLoop every 1 second.
     * No operational detail — just proves device is alive.
     */
    void sendStats(uint32_t records, uint16_t rate, uint32_t kb,
                   uint32_t epoch = 0) {
        (void)records; (void)rate; (void)kb;
        uint8_t next = (mHead + 1) & RING_MASK;
        if (next == mTail) return;
        snprintf(mRing[mHead], MSG_MAX_LEN,
                 "<14>arcana[%lu]: hb",
                 (unsigned long)epoch);
        mHead = next;
    }

    uint8_t pending() const {
        return static_cast<uint8_t>((mHead - mTail) & RING_MASK);
    }

private:
    static const uint8_t RING_SIZE    = 8;
    static const uint8_t RING_MASK    = RING_SIZE - 1;
    static const uint8_t MSG_MAX_LEN  = 64;
    static const uint16_t SYSLOG_PORT = 514;
    static constexpr const char* SYSLOG_HOST = "192.168.11.200";

    SyslogAppender() : mHead(0), mTail(0), mUdpOpen(false) {
        memset(mRing, 0, sizeof(mRing));
    }

    char mRing[RING_SIZE][MSG_MAX_LEN];   // 1280 bytes
    volatile uint8_t mHead;
    volatile uint8_t mTail;
    bool mUdpOpen;
};

} // namespace log
} // namespace arcana
