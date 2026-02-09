/**
 * @file CommandDispatcher.cpp
 * @brief Command dispatcher implementation
 */

#include "CommandDispatcher.hpp"

namespace arcana {

bool CommandDispatcher::executeCommand(const CommandRequest& request) {
    response_.reset();
    response_.key = request.key;

    ICommand* command = registry_.findCommand(request.key);
    if (command == nullptr) {
        response_.status = CommandStatus::NotFound;
        return false;
    }

    command->execute(request, response_);
    return true;
}

bool CommandDispatcher::dispatch(const CommandRequest& request) {
    executeCommand(request);
    return observable_.publish(&response_);
}

bool CommandDispatcher::dispatchSync(const CommandRequest& request) {
    executeCommand(request);
    observable_.notify(&response_);
    return response_.status == CommandStatus::Success;
}

} // namespace arcana
