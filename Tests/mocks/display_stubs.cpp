/**
 * @file display_stubs.cpp
 * @brief Host-side definition of display::g_display = nullptr.
 *
 * Inline DisplayStatus.hpp helpers (toastUpdate, toastRedraw) check
 * `if (!g_display) return;` and bail out, so for tests that don't draw a
 * real LCD we just provide a null pointer here.
 */
#include "IDisplay.hpp"

namespace arcana {
namespace display {
IDisplay* g_display = nullptr;
} // namespace display
} // namespace arcana
