/**
 * @file SerialAppender.hpp
 * @brief Log appender — printf to UART serial
 *
 * Format: [L][TAG] 0xCODE p=PARAM
 * All levels pass through (minLevel = Trace).
 */

#pragma once

#include "Log.hpp"
#include <cstdio>

namespace arcana {
namespace log {

class SerialAppender : public IAppender {
public:
    void append(const LogEvent& event) override {
        static const char LEVEL_CH[] = "TDIWEF";
        static const char* const SRC_TAG[] = {
            "SYS",  "SDIO", "SENS", "WiFi", "Pump",
            "Cryp", "ATS",  "NTP",  "MQTT", "BLE",
            "OTA",  "CMD",  "LCD",
        };
        static const uint8_t SRC_COUNT =
            sizeof(SRC_TAG) / sizeof(SRC_TAG[0]);

        char lvl = (event.level < sizeof(LEVEL_CH) - 1)
                   ? LEVEL_CH[event.level] : '?';
        const char* src = (event.source < SRC_COUNT)
                         ? SRC_TAG[event.source] : "???";

        if (event.param != 0) {
            printf("[%c][%s] 0x%04X p=%lu\r\n",
                   lvl, src,
                   (unsigned)event.code,
                   (unsigned long)event.param);
        } else {
            printf("[%c][%s] 0x%04X\r\n",
                   lvl, src,
                   (unsigned)event.code);
        }
    }

    Level minLevel() const override { return Level::Trace; }
};

} // namespace log
} // namespace arcana
