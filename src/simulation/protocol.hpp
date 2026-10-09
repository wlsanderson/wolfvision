#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace simulation {
constexpr std::size_t header_size = 48;
constexpr std::size_t fragment_size = 1100;
constexpr std::size_t max_unit_size = std::size_t{1024} * 1024;
enum class Type : std::uint8_t { video = 1, telemetry = 2, end = 3 };
struct Packet {
    Type type = Type::video;
    std::uint64_t session = 1, sequence = 0, timestamp_us = 0;
    std::uint32_t unit = 0, total_size = 0;
    std::uint16_t fragment = 0, fragments = 1;
    std::vector<std::uint8_t> payload;
};
std::vector<std::uint8_t> encode(const Packet& packet);
bool decode(const std::vector<std::uint8_t>& bytes, Packet& output, std::string& error);
std::vector<Packet> fragment_video(const std::vector<std::uint8_t>& unit, std::uint32_t id);
} // namespace simulation
