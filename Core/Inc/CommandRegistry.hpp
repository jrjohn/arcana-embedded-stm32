/**
 * @file CommandRegistry.hpp
 * @brief Static registry for command handlers
 */

#ifndef ARCANA_COMMAND_REGISTRY_HPP
#define ARCANA_COMMAND_REGISTRY_HPP

#include "ICommand.hpp"

namespace arcana {

/**
 * @brief Static command registry
 *
 * Holds pointers to command instances. Linear scan lookup — O(n) with n <= MAX_COMMANDS.
 * All storage is static (no heap allocation).
 */
class CommandRegistry {
public:
    static constexpr uint8_t MAX_COMMANDS = 8;

    /**
     * @brief Register a command handler
     * @param command Pointer to static command instance
     * @return true if registered successfully, false if full or duplicate
     */
    bool registerCommand(ICommand* command);

    /**
     * @brief Find a command by key
     * @param key Command key to search for
     * @return Pointer to command, or nullptr if not found
     */
    ICommand* findCommand(CommandKey key) const;

    /**
     * @brief Get the number of registered commands
     */
    uint8_t getCommandCount() const { return count_; }

private:
    ICommand* commands_[MAX_COMMANDS] = {};
    uint8_t count_ = 0;
};

} // namespace arcana

#endif /* ARCANA_COMMAND_REGISTRY_HPP */
