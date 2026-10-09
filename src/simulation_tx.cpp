#include "common/clock.hpp"
#include "common/udp.hpp"
#include "simulation/options.hpp"
#include "simulation/protocol.hpp"
#include "simulation/sources.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <random>
#include <thread>
int main(int argc, char** argv) {
    using namespace simulation;
    try {
        auto o = parse_options(argc, argv, false);
        if (o.help) {
            std::cout << usage(false);
            return 0;
        }
        if (!o.session) {
            std::random_device random;
            o.session = (static_cast<std::uint64_t>(random()) << 32) ^ random();
            if (!o.session)
                o.session = 1;
        }
        common::UdpSocket socket(o.address, o.port, false);
        SimulatedUartSource uart;
        std::uint64_t sequence = 0, samples = 0, video_bytes = 0;
        std::uint32_t unit = 0;
        const auto start = std::chrono::steady_clock::now();
        auto next_sample = start;
        const auto sample_period = std::chrono::microseconds(1000000 / o.telemetry_rate);
        auto send = [&](Packet& p) {
            if (sequence == UINT64_MAX)
                throw std::runtime_error("sequence exhausted");
            p.session = o.session;
            p.sequence = sequence++;
            if (!p.timestamp_us)
                p.timestamp_us = common::monotonic_microseconds();
            const auto bytes = encode(p);
            socket.send(bytes.data(), bytes.size());
        };
        auto send_sample = [&] {
            Packet p;
            p.type = Type::telemetry;
            p.total_size = 32;
            p.timestamp_us = common::monotonic_microseconds();
            const auto stamped = telemetry::encode({samples, p.timestamp_us, uart.sample(samples)});
            p.payload.assign(stamped.begin(), stamped.end());
            send(p);
            ++samples;
        };
        std::cout << "SIMULATION session=" << o.session << " prerecorded H.264 + SIMULATED UART\n";
        for (std::uint64_t loop = 0; loop < o.loops; ++loop) {
            H264FileSource source(o.input);
            std::vector<std::uint8_t> bytes;
            bool any = false;
            while (source.next(bytes)) {
                any = true;
                if (unit == UINT32_MAX)
                    throw std::runtime_error("access unit counter exhausted");
                const auto due = start + std::chrono::microseconds(static_cast<std::int64_t>(unit) *
                                                                   1000000 / o.fps);
                while (std::chrono::steady_clock::now() < due) {
                    if (std::chrono::steady_clock::now() >= next_sample) {
                        send_sample();
                        next_sample += sample_period;
                    }
                    std::this_thread::sleep_until(std::min(due, next_sample));
                }
                if (std::chrono::steady_clock::now() >= next_sample) {
                    send_sample();
                    next_sample = std::chrono::steady_clock::now() +
                                  sample_period; // Drop missed ticks, no catch-up queue.
                }
                auto fragments = fragment_video(bytes, unit);
                for (auto& p : fragments)
                    send(p);
                video_bytes += bytes.size();
                ++unit;
            }
            if (!any)
                throw std::runtime_error("empty H.264 input");
        }
        Packet end;
        end.type = Type::end;
        end.unit = unit;
        send(end);
        const auto bytes = encode(end);
        for (int retry = 0; retry < 2; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            socket.send(bytes.data(), bytes.size());
        }
        std::cout << "Sent access_units=" << unit << " video_bytes=" << video_bytes
                  << " telemetry_samples=" << samples << " datagrams=" << sequence + 2 << '\n';
    } catch (const std::exception& e) {
        std::cerr << "simulation_tx: " << e.what() << '\n' << usage(false);
        return 1;
    }
}
