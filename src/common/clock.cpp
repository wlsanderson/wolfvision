#include "common/clock.hpp"
#include <chrono>
namespace common {
std::uint64_t monotonic_microseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
} // namespace common
