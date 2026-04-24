#pragma once

#include "ServiceTypes.hpp"
#include <cstdint>

namespace arcana {
namespace reg {

/**
 * Device registration service — TOFU provisioning.
 * First boot: POST /api/register → get MQTT credentials.
 * Subsequent boots: load stored credentials.
 */
class RegistrationService {
public:
    struct Credentials {
        char mqttUser[36];
        char mqttPass[36];
        char mqttBroker[36];
        uint16_t mqttPort;
        char uploadToken[72];
        char topicPrefix[36];
        uint8_t commKey[32];     // ECDH-derived symmetric key for MQTT encryption
        bool hasCommKey;         // true if comm_key was derived during registration
        bool valid;
    };

    virtual ~RegistrationService() {}
    virtual bool isRegistered() const = 0;
    virtual const Credentials& credentials() const = 0;
    virtual bool doRegistration() = 0;  // HTTP POST to server

protected:
    RegistrationService() {}
};

} // namespace reg
} // namespace arcana
