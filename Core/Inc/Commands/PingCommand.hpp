/**
 * @file PingCommand.hpp
 * @brief System::Ping command — returns current tick count
 */

#ifndef ARCANA_PING_COMMAND_HPP
#define ARCANA_PING_COMMAND_HPP

#include "ICommand.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace arcana {

/**
 * @brief Ping command (System cluster, ID 0x01)
 *
 * Returns the current FreeRTOS tick count as 4 bytes LE.
 * Used for connectivity checks and latency measurement.
 */
class PingCommand : public ICommand {
public:
    CommandKey getKey() const override {
        return {Cluster::System, SystemCommand::Ping};
    }

    void execute(const CommandRequest& request, CommandResponseModel& response) override {
        (void)request;
        uint32_t ticks = xTaskGetTickCount();
        response.setUint32(ticks);
        response.status = CommandStatus::Success;
    }
};

} // namespace arcana

#endif /* ARCANA_PING_COMMAND_HPP */
