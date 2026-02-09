/**
 * @file ICommand.hpp
 * @brief Abstract command interface
 */

#ifndef ARCANA_ICOMMAND_HPP
#define ARCANA_ICOMMAND_HPP

#include "CommandTypes.hpp"

namespace arcana {

/**
 * @brief Abstract command interface
 *
 * Concrete commands implement getKey() and execute().
 * Instances are statically allocated and registered at init time.
 */
class ICommand {
public:
    virtual ~ICommand() = default;

    /**
     * @brief Get the command key (cluster + id)
     */
    virtual CommandKey getKey() const = 0;

    /**
     * @brief Execute the command
     * @param request  Input parameters
     * @param response Output buffer (pre-reset by dispatcher)
     */
    virtual void execute(const CommandRequest& request, CommandResponseModel& response) = 0;
};

} // namespace arcana

#endif /* ARCANA_ICOMMAND_HPP */
