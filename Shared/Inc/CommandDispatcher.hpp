/**
 * @file CommandDispatcher.hpp
 * @brief Routes command requests through the registry
 */

#ifndef ARCANA_COMMAND_DISPATCHER_HPP
#define ARCANA_COMMAND_DISPATCHER_HPP

#include "CommandRegistry.hpp"

namespace arcana {

/**
 * @brief Command dispatcher
 *
 * Looks up commands in the registry, executes them, and publishes
 * the response via the Observable. Owns a shared response buffer
 * (one command at a time — no concurrent execution).
 */
class CommandDispatcher {
public:
    /**
     * @brief Construct dispatcher with references to registry and observable
     * @param registry Command registry
     * @param observable Response observable for async notification
     */
    CommandDispatcher(CommandRegistry& registry, Observable<CommandResponseModel>& observable)
        : registry_(registry), observable_(observable) {}

    /**
     * @brief Dispatch a command request (async — publishes response via observable)
     * @param request Command request
     * @return true if command was found and executed
     */
    bool dispatch(const CommandRequest& request);

    /**
     * @brief Dispatch a command request (synchronous — notifies observers directly)
     * @param request Command request
     * @return true if command was found and executed
     */
    bool dispatchSync(const CommandRequest& request);

private:
    CommandRegistry& registry_;
    Observable<CommandResponseModel>& observable_;
    CommandResponseModel response_;

    /**
     * @brief Execute a command and fill the response buffer
     * @param request Command request
     * @return true if command was found and executed
     */
    bool executeCommand(const CommandRequest& request);
};

} // namespace arcana

#endif /* ARCANA_COMMAND_DISPATCHER_HPP */
