#include "telemetry/measurement.hpp"
namespace telemetry {
Measurement simulated_measurement(std::uint64_t sample) {
    return {static_cast<std::uint32_t>(12000 + sample % 101),
            static_cast<std::int32_t>(25000 + (sample % 201) * 10)};
}
} // namespace telemetry
