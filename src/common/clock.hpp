#pragma once
#include <cstdint>
namespace common {
// Steady-clock microseconds, comparable only within this initial host test.
std::uint64_t monotonic_microseconds();
} // namespace common
