#include "telemetry/protocol.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace telemetry;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        const Packet sample{
            UINT64_C(0x0102030405060708), UINT64_C(0x1112131415161718), {12000, -1000}};
        const Bytes golden{'W', 'V', 'T', 'M', 1,  1,  0,  32, 1, 2, 3,  4,   5,   6,   7,   8,
                           17,  18,  19,  20,  21, 22, 23, 24, 0, 0, 46, 224, 255, 255, 252, 24};
        check(encode(sample) == golden, "wire bytes must match independent big-endian fixture");
        for (const auto temperature : {INT32_MIN, -1000, 0, INT32_MAX}) {
            for (const auto sequence : {UINT64_C(0), UINT64_MAX}) {
                const Packet original{sequence, UINT64_MAX, {UINT32_MAX, temperature}};
                const auto bytes = encode(original);
                Packet decoded{};
                std::string error;
                check(decode(bytes.data(), bytes.size(), decoded, error), "round trip rejected");
                check(decoded.sequence == original.sequence &&
                          decoded.monotonic_us == original.monotonic_us &&
                          decoded.measurement.supply_mv == original.measurement.supply_mv &&
                          decoded.measurement.temperature_mc == original.measurement.temperature_mc,
                      "round trip mismatch");
                check(error.empty(), "success must clear error");
            }
        }
        Packet output{42, 99, {1, 2}};
        std::string error;
        for (std::size_t length = 0; length < packet_size; ++length)
            check(!decode(golden.data(), length, output, error), "truncated packet accepted");
        auto oversized = std::vector<std::uint8_t>(golden.begin(), golden.end());
        oversized.push_back(0);
        check(!decode(oversized.data(), oversized.size(), output, error),
              "oversized packet accepted");
        check(!decode(nullptr, packet_size, output, error), "null data accepted");
        for (const auto offset : {0, 1, 2, 3, 4, 5, 6, 7}) {
            auto bad = golden;
            bad[offset] ^= 0x80;
            check(!decode(bad.data(), bad.size(), output, error), "invalid header accepted");
            check(!error.empty(), "rejection needs diagnostic");
            check(output.sequence == 42 && output.monotonic_us == 99 &&
                      output.measurement.supply_mv == 1 && output.measurement.temperature_mc == 2,
                  "rejection changed output");
        }
        std::cout << "Packet wire fixture, round trips, malformed packet tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
