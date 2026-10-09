#include "common/clock.hpp"
#include "common/sequence.hpp"
#include "common/udp.hpp"
#include "simulation/options.hpp"
#include "simulation/protocol.hpp"
#include "simulation/reassembly.hpp"
#include "telemetry/protocol.hpp"
#include <algorithm>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
namespace {
struct Capture {
    std::uint64_t session, started, last, end_at = 0, end_units = 0;
    std::uint64_t received = 0, rejected = 0, duplicates = 0, late = 0, gaps = 0, stale = 0,
                  rows = 0, max_delay = 0;
    bool ended = false;
    std::string base;
    std::ofstream video, csv;
    common::SequenceWindow sequence;
    simulation::Reassembler assembler;
    Capture(std::uint64_t id, std::uint64_t now, const simulation::Options& o)
        : session(id), started(now), last(now), base(o.prefix + "-" + std::to_string(id)),
          assembler(static_cast<std::uint64_t>(o.reorder_ms) * 1000, [this](const auto& bytes) {
              video.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
          }) {
        // Each session has independent outputs; never silently truncate an old capture.
        for (const auto* extension : {".h264", ".csv", ".json"})
            if (std::ifstream(base + extension).good())
                throw std::runtime_error("capture already exists: " + base + extension);
        video.exceptions(std::ios::failbit | std::ios::badbit);
        csv.exceptions(std::ios::failbit | std::ios::badbit);
        video.open(base + ".h264", std::ios::binary);
        csv.open(base + ".csv");
        csv << "session,sequence,monotonic_us,supply_mv,temperature_mc,sequence_status\n";
        std::cout << "SESSION " << session << " output=" << base << " SIMULATED UART\n";
    }
    void finish(std::uint64_t now, const std::string& reason) {
        const auto final_units = ended ? end_units : assembler.highest_unit_end();
        assembler.finish(static_cast<std::uint32_t>(final_units), now);
        video.close();
        csv.close();
        const auto& s = assembler.stats();
        std::ofstream json;
        json.exceptions(std::ios::failbit | std::ios::badbit);
        json.open(base + ".json");
        json << "{\n  \"session\": " << session << ",\n  \"reason\": \"" << reason << "\",\n"
             << "  \"received_packets\": " << received << ",\n  \"rejected_packets\": " << rejected
             << ",\n  \"duplicates\": " << duplicates << ",\n  \"out_of_order\": " << late
             << ",\n  \"gap_observations\": " << gaps << ",\n  \"stale_packets\": " << stale
             << ",\n  \"telemetry_rows\": " << rows << ",\n  \"video_units_written\": " << s.written
             << ",\n  \"video_units_missing\": " << s.missing
             << ",\n  \"video_units_skipped_until_idr\": " << s.skipped
             << ",\n  \"invalid_video_units\": " << s.invalid << ",\n  \"video_bytes\": " << s.bytes
             << ",\n  \"peak_buffer_bytes\": " << s.peak_bytes
             << ",\n  \"peak_buffer_units\": " << s.peak_units
             << ",\n  \"sequence_entries\": " << sequence.size()
             << ",\n  \"max_local_packet_delay_us\": " << max_delay
             << ",\n  \"elapsed_us\": " << now - started << ",\n  \"video_bytes_per_second\": "
             << (now > started
                     ? static_cast<double>(s.bytes) * 1000000.0 / static_cast<double>(now - started)
                     : 0)
             << "\n}\n";
        json.close();
        std::cout << "Completed session=" << session << " reason=" << reason
                  << " video_units=" << s.written << " missing=" << s.missing
                  << " telemetry=" << rows << " peak_buffer_bytes=" << s.peak_bytes << '\n'
                  << std::flush;
    }
};
} // namespace
int main(int argc, char** argv) {
    using namespace simulation;
    using common::Arrival;
    try {
        const auto o = parse_options(argc, argv, true);
        if (o.help) {
            std::cout << usage(true);
            return 0;
        }
        common::UdpSocket socket(o.address, o.port, true);
        // The test runner reads readiness through a pipe; flush before receiving.
        std::cout << "READY " << o.address << ':' << o.port << " SIMULATION\n" << std::flush;
        std::unique_ptr<Capture> capture;
        std::deque<std::uint64_t> retired;
        std::uint64_t completed = 0, idle_since = common::monotonic_microseconds();
        int exit_code = 0;
        auto finalize = [&](std::uint64_t now, const std::string& reason) {
            capture->finish(now, reason);
            retired.push_back(capture->session);
            if (retired.size() > 32)
                retired.pop_front();
            capture.reset();
            ++completed;
            idle_since = now;
            if (reason != "end")
                exit_code = 2;
        };
        std::vector<std::uint8_t> bytes;
        while (completed < o.sessions) {
            bool received = socket.receive(bytes, 10);
            const auto now = common::monotonic_microseconds();
            if (capture) {
                capture->assembler.drain(now);
                if (capture->ended && now >= capture->end_at) {
                    finalize(now, "end");
                    if (completed >= o.sessions)
                        break;
                } else if (now - capture->last >= static_cast<std::uint64_t>(o.timeout_ms) * 1000) {
                    finalize(now, "timeout");
                    if (completed >= o.sessions)
                        break;
                }
            } else if (now - idle_since >= static_cast<std::uint64_t>(o.timeout_ms) * 1000) {
                std::cerr << "Timed out waiting for a session\n";
                return 2;
            }
            if (!received)
                continue;
            Packet p;
            std::string error;
            if (!decode(bytes, p, error)) {
                if (capture)
                    ++capture->rejected;
                std::cerr << "Rejected: " << error << '\n';
                continue;
            }
            telemetry::Packet sample{};
            if (p.type == Type::telemetry &&
                (!telemetry::decode(p.payload.data(), p.payload.size(), sample, error) ||
                 sample.monotonic_us != p.timestamp_us)) {
                if (capture)
                    ++capture->rejected;
                if (error.empty())
                    error = "envelope/telemetry timestamp mismatch";
                std::cerr << "Rejected telemetry: " << error << '\n';
                continue;
            }
            if (std::find(retired.begin(), retired.end(), p.session) != retired.end())
                continue;
            if (capture && p.session != capture->session) {
                finalize(now, "restart");
                if (completed >= o.sessions)
                    break;
            }
            if (!capture)
                capture = std::make_unique<Capture>(p.session, now, o);
            auto& c = *capture;
            if (p.type == Type::end && p.unit < c.assembler.highest_unit_end()) {
                ++c.rejected;
                continue;
            }
            if (c.ended && ((p.type == Type::video && p.unit >= c.end_units) ||
                            (p.type == Type::end && p.unit != c.end_units))) {
                ++c.rejected;
                continue;
            }
            const auto arrival = c.sequence.observe(p.sequence);
            ++c.received;
            if (arrival.kind == Arrival::duplicate) {
                ++c.duplicates;
                continue;
            }
            if (arrival.kind == Arrival::stale) {
                ++c.stale;
                continue;
            }
            c.last = now; // Malformed, stale, and duplicate traffic cannot postpone timeout.
            if (arrival.kind == Arrival::out_of_order)
                ++c.late;
            if (arrival.kind == Arrival::gap)
                c.gaps += arrival.gap;
            if (p.timestamp_us <= now)
                c.max_delay = std::max(c.max_delay, now - p.timestamp_us);
            if (p.type == Type::video) {
                if (!c.assembler.offer(p, now))
                    ++c.rejected;
            } else if (p.type == Type::telemetry) {
                c.csv << p.session << ',' << sample.sequence << ',' << sample.monotonic_us << ','
                      << sample.measurement.supply_mv << ',' << sample.measurement.temperature_mc
                      << ',' << arrival_name(arrival.kind) << '\n';
                ++c.rows;
            } else if (!c.ended) {
                c.ended = true;
                c.end_units = p.unit;
                c.end_at = now + static_cast<std::uint64_t>(o.reorder_ms) * 2000;
            }
        }
        return exit_code;
    } catch (const std::exception& e) {
        std::cerr << "simulation_rx: " << e.what() << '\n' << usage(true);
        return 1;
    }
}
