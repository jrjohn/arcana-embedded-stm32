/**
 * @file Credentials.hpp (host test stub)
 *
 * The real Credentials.hpp is gitignored (contains WiFi/MQTT secrets) and
 * lives in Targets/stm32f103ze/Services/Common/. RegistrationServiceImpl.cpp
 * includes it transitively; on CI the file doesn't exist, so the build fails.
 *
 * This stub provides matching #defines as Credentials.hpp.ci (CI placeholder)
 * and is found first via the mocks/ -I path. It only matters at compile time —
 * RegistrationServiceImpl never actually reads these values from
 * Credentials.hpp; they're consumed by other services (Wifi, MQTT) compiled
 * separately.
 */
#pragma once

#define WIFI_SSID_VALUE      "ci-ssid"
#define WIFI_PASS_VALUE      "ci-password"
#define MQTT_BROKER_VALUE    "localhost"
#define MQTT_PORT_VALUE      1883
#define UPLOAD_SERVER_VALUE  "localhost"
#define UPLOAD_PORT_VALUE    8080
