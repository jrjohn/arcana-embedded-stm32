/**
 * @file CommandTypes.hpp
 * @brief Command Pattern type definitions for STM32
 *
 * Defines clusters, command keys, request/response structures.
 * All structures use static allocation suitable for 8KB RAM MCUs.
 */

#ifndef ARCANA_COMMAND_TYPES_HPP
#define ARCANA_COMMAND_TYPES_HPP

#include "Models.hpp"

namespace arcana {

/**
 * @brief Command cluster enumeration (first-level routing)
 */
enum class Cluster : uint8_t {
    System = 0x00,
    Sensor = 0x01,
};

/**
 * @brief Command status codes
 */
enum class CommandStatus : uint8_t {
    Success = 0,
    NotFound,
    InvalidParam,
    Busy,
    Error,
};

/**
 * @brief Two-byte command key (Cluster + CommandId)
 */
struct CommandKey {
    Cluster cluster;
    uint8_t commandId;

    bool operator==(const CommandKey& other) const {
        return cluster == other.cluster && commandId == other.commandId;
    }
};

/* Well-known command IDs */
namespace SystemCommand {
    constexpr uint8_t Ping = 0x01;
}

namespace SensorCommand {
    constexpr uint8_t GetCounter = 0x01;
}

/**
 * @brief Command request payload
 */
struct CommandRequest {
    CommandKey key;
    uint8_t params[8];
    uint8_t paramsLength = 0;
};

/**
 * @brief Command response model (publishable via Observable)
 */
class CommandResponseModel : public Model {
public:
    static constexpr uint8_t MAX_DATA_LENGTH = 16;

    CommandKey key = {Cluster::System, 0};
    CommandStatus status = CommandStatus::Success;
    uint8_t data[MAX_DATA_LENGTH] = {};
    uint8_t dataLength = 0;

    CommandResponseModel() : Model(static_cast<uint8_t>(ModelType::Command)) {}

    void reset() {
        updateTimestamp();
        key = {Cluster::System, 0};
        status = CommandStatus::Success;
        dataLength = 0;
    }

    /**
     * @brief Write a uint32_t value as 4 bytes LE into data
     */
    void setUint32(uint32_t value) {
        data[0] = static_cast<uint8_t>(value & 0xFF);
        data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        data[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        data[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
        dataLength = 4;
    }
};

} // namespace arcana

#endif /* ARCANA_COMMAND_TYPES_HPP */
