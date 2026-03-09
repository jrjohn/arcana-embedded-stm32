/**
 * @file CommandRegistry.cpp
 * @brief Command registry implementation
 */

#include "CommandRegistry.hpp"

namespace arcana {

bool CommandRegistry::registerCommand(ICommand* command) {
    if (command == nullptr) return false;
    if (count_ >= MAX_COMMANDS) return false;

    /* Check for duplicate key */
    CommandKey key = command->getKey();
    for (uint8_t i = 0; i < count_; i++) {
        if (commands_[i]->getKey() == key) {
            return false;
        }
    }

    commands_[count_] = command;
    count_++;
    return true;
}

ICommand* CommandRegistry::findCommand(CommandKey key) const {
    for (uint8_t i = 0; i < count_; i++) {
        if (commands_[i]->getKey() == key) {
            return commands_[i];
        }
    }
    return nullptr;
}

} // namespace arcana
