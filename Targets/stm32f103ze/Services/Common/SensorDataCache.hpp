#pragma once

#include <cstdint>

namespace arcana {

/**
 * Cached latest sensor readings — shared between BLE JSON push and Command handlers.
 * Updated by observer callbacks, read by command execute().
 */
struct SensorDataCache {
    float    temp;
    int16_t  ax, ay, az;
    uint16_t als, ps;

    SensorDataCache()
        : temp(0), ax(0), ay(0), az(0), als(0), ps(0) {}
};

} // namespace arcana
