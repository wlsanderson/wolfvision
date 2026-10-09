#include "simulation/protocol.hpp"
#include <algorithm>
#include <stdexcept>
namespace simulation {
namespace {
void put(std::vector<std::uint8_t>& b, std::size_t pos, std::uint64_t v, std::size_t n) {
    while (n) {
        b[pos + --n] = static_cast<std::uint8_t>(v);
        v >>= 8;
    }
}
std::uint64_t get(const std::vector<std::uint8_t>& b, std::size_t pos, std::size_t n) {
    std::uint64_t v = 0;
    while (n--)
        v = (v << 8) | b[pos++];
    return v;
}
bool valid(const Packet& p, std::string& error) {
    if (!p.session)
        error = "zero session";
    else if (p.type == Type::video) {
        if (p.unit == UINT32_MAX || !p.total_size || p.total_size > max_unit_size)
            error = "invalid video size";
        else if (p.fragments != (p.total_size + fragment_size - 1) / fragment_size ||
                 p.fragment >= p.fragments)
            error = "invalid fragment geometry";
        else if (p.payload.size() !=
                 std::min<std::size_t>(fragment_size, p.total_size - p.fragment * fragment_size))
            error = "wrong fragment length";
        else
            return true;
    } else if (p.type == Type::telemetry || p.type == Type::end) {
        const std::size_t size = p.type == Type::telemetry ? 32 : 0;
        if ((p.type == Type::telemetry && p.unit) || p.fragment || p.fragments != 1 ||
            p.total_size != size || p.payload.size() != size)
            error = "invalid control payload";
        else
            return true;
    } else
        error = "unsupported packet type";
    return false;
}
} // namespace
std::vector<std::uint8_t> encode(const Packet& p) {
    std::string error;
    if (!valid(p, error))
        throw std::runtime_error(error);
    std::vector<std::uint8_t> b(header_size + p.payload.size());
    b[0] = 'W';
    b[1] = 'V';
    b[2] = 'S';
    b[3] = 'M';
    b[4] = 1;
    b[5] = static_cast<std::uint8_t>(p.type);
    put(b, 6, 1, 2); // Simulated inputs flag, all other bits zero.
    put(b, 8, p.session, 8);
    put(b, 16, p.sequence, 8);
    put(b, 24, p.timestamp_us, 8);
    put(b, 32, p.unit, 4);
    put(b, 36, p.fragment, 2);
    put(b, 38, p.fragments, 2);
    put(b, 40, p.total_size, 4);
    put(b, 44, p.payload.size(), 2); // 46..47 reserved zero.
    std::copy(p.payload.begin(), p.payload.end(), b.begin() + header_size);
    return b;
}
bool decode(const std::vector<std::uint8_t>& b, Packet& output, std::string& error) {
    if (b.size() < header_size || b.size() > header_size + fragment_size)
        error = "invalid datagram size";
    else if (b[0] != 'W' || b[1] != 'V' || b[2] != 'S' || b[3] != 'M')
        error = "invalid magic";
    else if (b[4] != 1)
        error = "unsupported version";
    else if (get(b, 6, 2) != 1 || get(b, 46, 2))
        error = "unsupported flags or reserved bits";
    else if (get(b, 44, 2) != b.size() - header_size)
        error = "invalid payload length";
    else {
        Packet p;
        p.type = static_cast<Type>(b[5]);
        p.session = get(b, 8, 8);
        p.sequence = get(b, 16, 8);
        p.timestamp_us = get(b, 24, 8);
        p.unit = static_cast<std::uint32_t>(get(b, 32, 4));
        p.fragment = static_cast<std::uint16_t>(get(b, 36, 2));
        p.fragments = static_cast<std::uint16_t>(get(b, 38, 2));
        p.total_size = static_cast<std::uint32_t>(get(b, 40, 4));
        p.payload.assign(b.begin() + header_size, b.end());
        if (!valid(p, error))
            return false;
        output = std::move(p);
        error.clear();
        return true;
    }
    return false;
}
std::vector<Packet> fragment_video(const std::vector<std::uint8_t>& unit, std::uint32_t id) {
    if (unit.empty() || unit.size() > max_unit_size)
        throw std::runtime_error("access unit exceeds 1 MiB or is empty");
    std::vector<Packet> result;
    const auto count =
        static_cast<std::uint16_t>((unit.size() + fragment_size - 1) / fragment_size);
    result.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        Packet p;
        p.unit = id;
        p.total_size = static_cast<std::uint32_t>(unit.size());
        p.fragments = count;
        p.fragment = index;
        const auto start = index * fragment_size;
        p.payload.assign(unit.begin() + static_cast<std::ptrdiff_t>(start),
                         unit.begin() + static_cast<std::ptrdiff_t>(
                                            std::min(unit.size(), start + fragment_size)));
        result.push_back(std::move(p));
    }
    return result;
}
} // namespace simulation
