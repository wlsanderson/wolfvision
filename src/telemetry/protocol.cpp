#include "telemetry/protocol.hpp"
#include <limits>
namespace telemetry {
namespace {
void put(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        bytes[offset + width - 1 - i] = static_cast<std::uint8_t>(value & 0xff);
        value >>= 8;
    }
}
std::uint64_t get(const std::uint8_t* data, std::size_t offset, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i)
        value = (value << 8) | data[offset + i];
    return value;
}
} // namespace
Bytes encode(const Packet& packet) {
    Bytes bytes{};
    bytes[0] = 'W';
    bytes[1] = 'V';
    bytes[2] = 'T';
    bytes[3] = 'M';
    bytes[4] = 1;
    bytes[5] = 1; // Version 1; simulated measurements.
    put(bytes, 6, packet_size, 2);
    put(bytes, 8, packet.sequence, 8);
    put(bytes, 16, packet.monotonic_us, 8);
    put(bytes, 24, packet.measurement.supply_mv, 4);
    put(bytes, 28, static_cast<std::uint32_t>(packet.measurement.temperature_mc), 4);
    return bytes;
}
bool decode(const std::uint8_t* data, std::size_t size, Packet& output, std::string& error) {
    if (size != packet_size)
        error = "wrong datagram size";
    else if (!data)
        error = "null packet data";
    else if (data[0] != 'W' || data[1] != 'V' || data[2] != 'T' || data[3] != 'M')
        error = "invalid magic";
    else if (data[4] != 1)
        error = "unsupported protocol version";
    else if (data[5] != 1)
        error = "unsupported flags (version 1 requires simulated data)";
    else if (get(data, 6, 2) != packet_size)
        error = "invalid declared length";
    else {
        const auto raw = get(data, 28, 4);
        const std::int64_t temperature = raw <= INT32_MAX
                                             ? static_cast<std::int64_t>(raw)
                                             : static_cast<std::int64_t>(raw) - (INT64_C(1) << 32);
        output = Packet{
            get(data, 8, 8),
            get(data, 16, 8),
            {static_cast<std::uint32_t>(get(data, 24, 4)), static_cast<std::int32_t>(temperature)}};
        error.clear();
        return true;
    }
    return false;
}
} // namespace telemetry
