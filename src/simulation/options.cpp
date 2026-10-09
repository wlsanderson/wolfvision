#include "simulation/options.hpp"
#include <charconv>
#include <stdexcept>
namespace simulation {
namespace {
std::uint64_t integer(const std::string& v) {
    std::uint64_t n = 0;
    const auto result = std::from_chars(v.data(), v.data() + v.size(), n);
    if (v.empty() || result.ec != std::errc{} || result.ptr != v.data() + v.size())
        throw std::runtime_error("expected unsigned integer: " + v);
    return n;
}
int bounded(const std::string& v, int low, int high) {
    const auto n = integer(v);
    if (n < static_cast<std::uint64_t>(low) || n > static_cast<std::uint64_t>(high))
        throw std::runtime_error("option out of range: " + v);
    return static_cast<int>(n);
}
} // namespace
Options parse_options(int argc, char** argv, bool receiver) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help") {
            o.help = true;
            continue;
        }
        if (++i == argc)
            throw std::runtime_error("missing value for " + key);
        const std::string v = argv[i];
        if (key == "--address")
            o.address = v;
        else if (key == "--port")
            o.port = static_cast<std::uint16_t>(bounded(v, 1, 65535));
        else if (key == "--input" && !receiver)
            o.input = v;
        else if (key == "--fps" && !receiver)
            o.fps = bounded(v, 1, 240);
        else if (key == "--telemetry-rate" && !receiver)
            o.telemetry_rate = bounded(v, 1, 1000);
        else if (key == "--loops" && !receiver) {
            o.loops = integer(v);
            if (!o.loops)
                throw std::runtime_error("loops must be positive");
        } else if (key == "--session" && !receiver) {
            o.session = integer(v);
            if (!o.session)
                throw std::runtime_error("session must be positive");
        } else if (key == "--output-prefix" && receiver)
            o.prefix = v;
        else if (key == "--sessions" && receiver)
            o.sessions = static_cast<std::uint64_t>(bounded(v, 1, 100000));
        else if (key == "--timeout-ms" && receiver)
            o.timeout_ms = bounded(v, 100, 600000);
        else if (key == "--reorder-ms" && receiver)
            o.reorder_ms = bounded(v, 1, 5000);
        else
            throw std::runtime_error("unknown option: " + key);
    }
    if (!receiver && !o.help && o.input.empty())
        throw std::runtime_error("--input is required");
    return o;
}
std::string usage(bool receiver) {
    return std::string(receiver ? "simulation_rx" : "simulation_tx") +
           " [--address IPv4] [--port N] " +
           (receiver ? "[--output-prefix PATH] [--sessions N] [--timeout-ms N] [--reorder-ms N]\n"
                     : "--input AUD-delimited.h264 [--fps N] [--telemetry-rate Hz] [--loops N] "
                       "[--session N]\n");
}
} // namespace simulation
