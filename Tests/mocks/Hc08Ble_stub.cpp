/**
 * @file Hc08Ble_stub.cpp
 * @brief Out-of-line definition for the host Hc08Ble singleton.
 */
#include "Hc08Ble.hpp"

namespace arcana {

Hc08Ble& Hc08Ble::getInstance() {
    static Hc08Ble sInstance;
    return sInstance;
}

} // namespace arcana
