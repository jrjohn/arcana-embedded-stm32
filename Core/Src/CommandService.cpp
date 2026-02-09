/**
 * @file CommandService.cpp
 * @brief Command service implementation with built-in command instances
 */

#include "CommandService.hpp"
#include "Commands/PingCommand.hpp"
#include "Commands/GetCounterCommand.hpp"

namespace arcana {

/* Global instance */
CommandService commandService;

/* Static command instances */
static PingCommand pingCommand;
static GetCounterCommand getCounterCommand;

void CommandService::init() {
    registry_.registerCommand(&pingCommand);
    registry_.registerCommand(&getCounterCommand);
}

} // namespace arcana
