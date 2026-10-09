#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace telemetry {
constexpr std::size_t packet_size = 32;
struct Measurement {
    std::uint32_t supply_mv;
    std::int32_t temperature_mc;
};
struct Packet {
    std::uint64_t sequence;
    std::uint64_t monotonic_us;
    Measurement measurement;
};
using Bytes = std::array<std::uint8_t, packet_size>;
Bytes encode(const Packet& packet);
// On failure, leaves output unchanged and supplies a diagnostic.
bool decode(const std::uint8_t* data, std::size_t size, Packet& output, std::string& error);
} // namespace telemetry
