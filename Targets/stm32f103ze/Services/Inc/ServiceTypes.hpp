#pragma once

#include <cstdint>

namespace arcana {

enum class ServiceStatus : uint8_t {
    OK = 0,
    Error,
    Busy,
    NotReady,
    InvalidState
};

} // namespace arcana
