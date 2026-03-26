#pragma once

#include "RegistrationService.hpp"
#include "Esp8266.hpp"

namespace arcana {
namespace reg {

class RegistrationServiceImpl : public RegistrationService {
public:
    static RegistrationServiceImpl& getInstance();

    bool isRegistered() const override { return mCreds.valid; }
    const Credentials& credentials() const override { return mCreds; }

    /**
     * Attempt registration via HTTP POST to server.
     * Uses ESP8266 TCP — caller must ensure WiFi is connected.
     * @param esp  ESP8266 instance
     * @return true if registered (or already was)
     */
    bool doRegistration() override;

    /** Load credentials from device.ats CONFIG. Call at boot. */
    bool loadCredentials();

    /** Save credentials to device.ats CONFIG. */
    bool saveCredentials();

    /** Get 8-char device ID (hex of hardware UID) */
    const char* deviceId() const { return mDeviceId; }

private:
    RegistrationServiceImpl();

    bool httpRegister(Esp8266& esp);
    bool parseResponse(const uint8_t* payload, uint16_t len);

    Credentials mCreds;
    char mDeviceId[9];    // "32FFD605\0"
    uint8_t mDeviceKey[32]; // derived from UID (used as public_key for TOFU)
};

} // namespace reg
} // namespace arcana
