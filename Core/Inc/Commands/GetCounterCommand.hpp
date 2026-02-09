/**
 * @file GetCounterCommand.hpp
 * @brief Sensor::GetCounter command — reads CounterService value
 */

#ifndef ARCANA_GET_COUNTER_COMMAND_HPP
#define ARCANA_GET_COUNTER_COMMAND_HPP

#include "ICommand.hpp"
#include "CounterService.hpp"

namespace arcana {

/**
 * @brief GetCounter command (Sensor cluster, ID 0x01)
 *
 * Returns the current counter value as 4 bytes LE.
 */
class GetCounterCommand : public ICommand {
public:
    CommandKey getKey() const override {
        return {Cluster::Sensor, SensorCommand::GetCounter};
    }

    void execute(const CommandRequest& request, CommandResponseModel& response) override {
        (void)request;
        uint32_t count = counterService.getCount();
        response.setUint32(count);
        response.status = CommandStatus::Success;
    }
};

} // namespace arcana

#endif /* ARCANA_GET_COUNTER_COMMAND_HPP */
