#pragma once

#include "ServiceTypes.hpp"

namespace arcana {

class Esp8266;  // forward declare

namespace wifi {

class WifiService {
public:
    virtual ~WifiService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

    // Full reset ESP8266 + AT test + connect WiFi
    virtual bool resetAndConnect() = 0;

    // Connect WiFi without ESP reset (for reconnect when ESP is still responsive)
    virtual bool connect() = 0;

    // NTP time sync via ESP8266 SNTP → SystemClock::sync()
    virtual bool syncNtp() = 0;

    // Access ESP8266 driver (shared with MqttService)
    virtual Esp8266& getEsp() = 0;

protected:
    WifiService() {}
};

} // namespace wifi
} // namespace arcana
