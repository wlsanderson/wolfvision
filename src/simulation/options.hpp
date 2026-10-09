#pragma once
#include <cstdint>
#include <string>
namespace simulation {
struct Options {
    std::string address = "127.0.0.1", input, prefix = "capture";
    std::uint16_t port = 5100;
    std::uint64_t session = 0, loops = 1, sessions = 1;
    int fps = 20, telemetry_rate = 10, timeout_ms = 3000, reorder_ms = 150;
    bool help = false;
};
Options parse_options(int argc, char** argv, bool receiver);
std::string usage(bool receiver);
} // namespace simulation
