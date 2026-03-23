#pragma once

#include "WifiService.hpp"
#include "Esp8266.hpp"

namespace arcana {
namespace wifi {

class WifiServiceImpl : public WifiService {
public:
    static WifiService& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    bool resetAndConnect() override;
    bool connect() override;
    bool syncNtp() override;
    Esp8266& getEsp() override;

    /** Query IP geolocation for timezone offset (minutes). Returns true on success. */
    bool detectTimezone(int16_t& offsetMinutes);

private:
    WifiServiceImpl();
    ~WifiServiceImpl();

    bool connectWifi();

    // Apply parsed epoch to SystemClock (returns false if epoch invalid)
    bool applyNtpEpoch(uint32_t epoch);

    // Configuration
    static const char* WIFI_SSID;
    static const char* WIFI_PASS;

    Esp8266& mEsp;
};

} // namespace wifi
} // namespace arcana
