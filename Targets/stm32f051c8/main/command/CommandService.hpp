/**
 * @file CommandService.hpp
 * @brief Command pattern facade — owns registry, dispatcher, and observable
 */

#ifndef ARCANA_COMMAND_SERVICE_HPP
#define ARCANA_COMMAND_SERVICE_HPP

#include "CommandDispatcher.hpp"

namespace arcana {

/**
 * @brief Command service facade
 *
 * Top-level entry point for the command pattern.
 * Owns the registry, observable, and dispatcher.
 * Registers built-in commands on init().
 */
class CommandService {
public:
    Observable<CommandResponseModel> observable{"Command"};

    /**
     * @brief Initialize the command service and register built-in commands
     */
    void init();

    /**
     * @brief Execute a command request (async via observable)
     */
    bool execute(const CommandRequest& request) {
        return dispatcher_.dispatch(request);
    }

    /**
     * @brief Execute a command request (synchronous)
     */
    bool executeSync(const CommandRequest& request) {
        return dispatcher_.dispatchSync(request);
    }

    /**
     * @brief Get the number of registered commands
     */
    uint8_t getCommandCount() const { return registry_.getCommandCount(); }

    /**
     * @brief Get reference to the registry (for external command registration)
     */
    CommandRegistry& getRegistry() { return registry_; }

private:
    CommandRegistry registry_;
    CommandDispatcher dispatcher_{registry_, observable};
};

/* Global instance */
extern CommandService commandService;

} // namespace arcana

#endif /* ARCANA_COMMAND_SERVICE_HPP */
