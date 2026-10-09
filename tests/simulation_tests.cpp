#include "simulation/protocol.hpp"
#include "simulation/reassembly.hpp"
#include "simulation/sources.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace simulation;
void check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
std::vector<std::uint8_t> frame(bool idr = true, std::size_t size = 32) {
    std::vector<std::uint8_t> b = {0, 0, 0, 1, 9, 0xf0};
    if (idr)
        b.insert(b.end(), {0, 0, 0, 1, 7, 0x42, 0, 0, 0, 1, 8, 0x42});
    b.insert(b.end(), {0, 0, 1, static_cast<std::uint8_t>(idr ? 0x65 : 0x41)});
    b.resize(std::max(size, b.size()), 0x55);
    return b;
}
int main() {
    try {
        Packet p;
        p.session = UINT64_C(0x0102030405060708);
        p.sequence = UINT64_MAX;
        p.timestamp_us = 0x11223344;
        p.type = Type::telemetry;
        p.total_size = 32;
        p.payload.resize(32, 0x77);
        const auto bytes = encode(p);
        check(bytes.size() == 80 && bytes[0] == 'W' && bytes[4] == 1 && bytes[5] == 2 &&
                  bytes[7] == 1 && bytes[8] == 1 && bytes[15] == 8 && bytes[16] == 255 &&
                  bytes[23] == 255 && bytes[28] == 0x11 && bytes[31] == 0x44 && bytes[43] == 32 &&
                  bytes[45] == 32,
              "independent wire offsets");
        Packet decoded;
        std::string error;
        check(decode(bytes, decoded, error) && decoded.session == p.session &&
                  decoded.sequence == p.sequence && decoded.timestamp_us == p.timestamp_us &&
                  decoded.payload == p.payload,
              "envelope roundtrip");
        for (const auto offset : {0, 4, 5, 6, 7, 8, 38, 40, 44, 46}) {
            auto bad = bytes;
            bad[offset] ^= 0x80;
            if (offset == 8) {
                for (int i = 8; i < 16; ++i)
                    bad[i] = 0;
            }
            check(!decode(bad, decoded, error), "malformed envelope accepted");
        }
        for (std::size_t i = 0; i < header_size; ++i)
            check(!decode(std::vector<std::uint8_t>(bytes.begin(),
                                                    bytes.begin() + static_cast<std::ptrdiff_t>(i)),
                          decoded, error),
                  "truncation accepted");
        auto extra = bytes;
        extra.push_back(0);
        check(!decode(extra, decoded, error), "extra bytes accepted");
        const auto original = frame(true, 3000);
        auto fragments = fragment_video(original, 0);
        check(fragments.size() == 3, "fragment count");
        for (const auto& packet : fragments) {
            check(decode(encode(packet), decoded, error) && decoded.fragment == packet.fragment &&
                      decoded.payload == packet.payload,
                  "fragment roundtrip");
        }
        auto bad = encode(fragments.back());
        bad[37] = 3;
        check(!decode(bad, decoded, error), "invalid index accepted");
        bool threw = false;
        try {
            fragment_video(std::vector<std::uint8_t>(max_unit_size + 1), 0);
        } catch (...) {
            threw = true;
        }
        check(threw, "oversized unit accepted");
        UartParser uart;
        check(uart.feed("SIM,12000,").empty(), "partial UART line emitted");
        auto measurements = uart.feed("-250\nSIM,12100,25000\n");
        check(measurements.size() == 2 && measurements[0].temperature_mc == -250,
              "UART chunks/multiple frames");
        uart.feed("bad\nSIM,4294967296,1\nSIM,1,2147483648\n" + std::string(200, 'x') + "\n");
        check(uart.rejected() == 4 && uart.feed("SIM,1,2\n").size() == 1,
              "UART rejection/resynchronization");
        std::vector<std::vector<std::uint8_t>> written;
        Reassembler assembly(100, [&](const auto& unit) { written.push_back(unit); });
        assembly.offer(fragments[2], 0);
        assembly.offer(fragments[2], 0);
        assembly.offer(fragments[0], 0);
        check(written.empty(), "partial unit emitted");
        auto conflict = fragments[0];
        conflict.payload.back() ^= 1;
        check(!assembly.offer(conflict, 0), "conflicting duplicate accepted");
        assembly.offer(fragments[1], 0);
        check(written.size() == 1 && written[0] == original, "reordered fragments reconstruction");
        auto missing = fragment_video(frame(false, 3000), 1);
        assembly.offer(missing[0], 0);
        for (const auto& packet : fragment_video(frame(false), 2))
            assembly.offer(packet, 0);
        assembly.drain(101);
        check(written.size() == 1 && assembly.stats().missing == 1 && assembly.stats().skipped == 1,
              "loss waits for IDR");
        for (const auto& packet : fragment_video(frame(true), 3))
            assembly.offer(packet, 102);
        check(written.size() == 2, "IDR recovery");
        assembly.offer(missing[1], 103);
        check(written.size() == 2, "expired frame revived");
        assembly.finish(6, 104);
        check(assembly.stats().missing == 3, "trailing loss at end");
        check(!assembly.offer(Packet{}, 105), "empty fragment accepted by reassembler");
        Reassembler invalid(100, [](const auto&) {});
        const std::vector<std::uint8_t> bad_video = {0, 0, 1, 9, 0xf0}; // AUD only; no picture.
        invalid.offer(fragment_video(bad_video, 0)[0], 0);
        check(invalid.stats().invalid == 1 && invalid.stats().written == 0,
              "invalid completed video emitted");
        Reassembler reorder(100, [&](const auto& unit) { written.push_back(unit); });
        for (const auto& packet : fragment_video(frame(false), 1))
            reorder.offer(packet, 0);
        for (const auto& packet : fragment_video(frame(true), 0))
            reorder.offer(packet, 99);
        check(reorder.stats().written == 2 && reorder.stats().missing == 0, "whole-unit reorder");
        Reassembler bounded(1000000, [](const auto&) {});
        auto huge = fragment_video(frame(true, max_unit_size), 0);
        for (std::uint32_t id = 0; id < 200; ++id) {
            for (std::size_t j = 0; j + 1 < huge.size(); ++j) {
                auto part = huge[j];
                part.unit = id;
                bounded.offer(part, id);
            }
        }
        check(bounded.stats().peak_bytes <= Reassembler::max_buffer_bytes &&
                  bounded.stats().peak_units <= Reassembler::max_units,
              "buffer bound");
        auto distant = fragment_video(frame(true), UINT32_MAX - 1);
        bounded.offer(distant[0], 1000001);
        bounded.drain(2000002);
        check(bounded.buffered_bytes() == 0, "large gap must drain efficiently");
        Reassembler window(1000000, [](const auto&) {});
        for (std::uint32_t id = 0; id < 100; ++id) {
            auto packet = fragments[0];
            packet.unit = id;
            window.offer(packet, id);
        }
        check(window.stats().peak_units == Reassembler::max_units && window.stats().missing == 36,
              "unit window eviction must be bounded and counted");
        window.drain(1000100);
        check(window.buffered_bytes() == 0 && window.stats().missing == 100,
              "expired window must clear");
        const auto path =
            std::filesystem::temp_directory_path() /
            ("wolfvision-source-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".h264");
        check(!std::filesystem::exists(path), "test fixture path collision");
        {
            std::ofstream stream(path, std::ios::binary);
            const auto a = frame(true), b = frame(false);
            stream.write(reinterpret_cast<const char*>(a.data()),
                         static_cast<std::streamsize>(a.size()));
            stream.write(reinterpret_cast<const char*>(b.data()),
                         static_cast<std::streamsize>(b.size()));
        }
        H264FileSource source(path.string());
        std::vector<std::uint8_t> unit;
        check(source.next(unit) && unit == frame(true), "first file access unit");
        check(source.next(unit) && unit == frame(false) && !source.next(unit),
              "file boundaries/eof");
        {
            std::ofstream stream(path, std::ios::binary);
            const std::vector<std::uint8_t> no_aud = {0, 0, 1, 0x65, 0x55};
            stream.write(reinterpret_cast<const char*>(no_aud.data()),
                         static_cast<std::streamsize>(no_aud.size()));
        }
        threw = false;
        try {
            H264FileSource broken(path.string());
            broken.next(unit);
        } catch (...) {
            threw = true;
        }
        check(threw, "H.264 without AUD accepted");
        std::filesystem::remove(path);
        std::cout
            << "Simulation protocol, UART framing, reassembly/recovery, and memory bounds passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
