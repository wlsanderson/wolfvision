#pragma once
#include "telemetry/protocol.hpp"
namespace telemetry {
// Deterministic bench data, not readings from real sensors.
Measurement simulated_measurement(std::uint64_t sample);
} // namespace telemetry
