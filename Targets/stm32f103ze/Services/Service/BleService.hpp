#pragma once

#include "ServiceTypes.hpp"

namespace arcana {
namespace ble {

/**
 * BLE Service — command transport via HC-08.
 * Receives FrameCodec-framed commands from BLE peer,
 * executes via shared CommandBridge, sends response back.
 */
class BleService {
public:
    virtual ~BleService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    BleService() {}
};

} // namespace ble
} // namespace arcana
