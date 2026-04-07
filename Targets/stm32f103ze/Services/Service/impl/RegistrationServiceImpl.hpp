#pragma once

#include "RegistrationService.hpp"
#include "Esp8266.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

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

    /** Invalidate cached credentials (force re-register on next attempt) */
    void invalidate() { mCreds.valid = false; mForceRegister = true; }

    /** Get 8-char device ID (hex of hardware UID) */
    const char* deviceId() const { return mDeviceId; }

    /** Get comm_key (ECDH-derived). Falls back to device_key if not registered. */
    const uint8_t* getCommKey() const {
        return mCreds.hasCommKey ? mCreds.commKey : mDeviceKey;
    }

private:
    /* Test access — host gtest fixtures populate mCreds for "registered"
     * branches in MqttServiceImpl + HttpUploadServiceImpl. */
    friend struct RegistrationServiceTestAccess;

    RegistrationServiceImpl();

    bool httpRegister(Esp8266& esp);
    bool parseResponse(const uint8_t* payload, uint16_t len);

    Credentials mCreds;
    char mDeviceId[9];    // "32FFD605\0"
    uint8_t mDeviceKey[32]; // derived from UID (used as public_key for TOFU)
    bool mForceRegister = false;

    // Temporary storage for server response (used between parseResponse and comm_key)
    uint8_t mServerPub[64];
    uint8_t mServerPubLen = 0;
    uint8_t mEcdsaSig[72];
    uint8_t mEcdsaSigLen = 0;
};

} // namespace reg
} // namespace arcana
