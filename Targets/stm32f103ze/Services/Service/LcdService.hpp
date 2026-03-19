#pragma once

#include "IDisplay.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace lcd {

/**
 * LCD Service — hardware abstraction only.
 * ViewModel and View are wired by Controller, not owned by Service.
 */
class LcdService {
public:
    virtual ~LcdService() {}
    virtual ServiceStatus initHAL() = 0;
    virtual display::IDisplay& getDisplay() = 0;

protected:
    LcdService() {}
};

} // namespace lcd
} // namespace arcana
