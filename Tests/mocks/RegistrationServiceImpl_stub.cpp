/**
 * @file RegistrationServiceImpl_stub.cpp
 * @brief Minimal host stub for reg::RegistrationServiceImpl.
 *
 * BleServiceImpl::pushSensorEncrypted() and MqttServiceImpl::publishSensorData()
 * call `RegistrationServiceImpl::getInstance().getCommKey()` to grab the
 * 32-byte symmetric key. The real impl pulls the key from device.ats CONFIG +
 * runs an HTTP POST registration handshake — way too much for a focused BLE
 * test. Here we just instantiate the singleton with mDeviceKey = 0xAA repeated
 * (so tests can predict ChaCha20 output if they need to) and leave hasCommKey
 * = false so getCommKey() falls back to mDeviceKey.
 *
 * Test code can override the key via test_reg_set_device_key().
 */
#include "RegistrationServiceImpl.hpp"

#include <cstring>

namespace arcana {
namespace reg {

RegistrationServiceImpl::RegistrationServiceImpl() {
    std::memset(&mCreds, 0, sizeof(mCreds));
    mCreds.hasCommKey = false;
    mCreds.valid      = false;
    std::memset(mDeviceId, 0, sizeof(mDeviceId));
    std::memcpy(mDeviceId, "TEST1234", 8);
    for (int i = 0; i < 32; ++i) mDeviceKey[i] = 0xAA;
    std::memset(mServerPub, 0, sizeof(mServerPub));
    mServerPubLen = 0;
    std::memset(mEcdsaSig, 0, sizeof(mEcdsaSig));
    mEcdsaSigLen  = 0;
}

RegistrationServiceImpl& RegistrationServiceImpl::getInstance() {
    static RegistrationServiceImpl sInstance;
    return sInstance;
}

bool RegistrationServiceImpl::doRegistration() { return true; }
bool RegistrationServiceImpl::loadCredentials() { return true; }
bool RegistrationServiceImpl::saveCredentials() { return true; }

bool RegistrationServiceImpl::httpRegister(Esp8266& /*esp*/) { return true; }
bool RegistrationServiceImpl::parseResponse(const uint8_t* /*payload*/,
                                             uint16_t /*len*/) {
    return true;
}

} // namespace reg
} // namespace arcana
