#include "simulation/sources.hpp"
#include "simulation/protocol.hpp"
#include "telemetry/measurement.hpp"
#include <charconv>
#include <stdexcept>
namespace simulation {
namespace {
std::size_t start_code(const std::vector<std::uint8_t>& b, std::size_t i) {
    if (i + 3 <= b.size() && b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1)
        return 3;
    if (i + 4 <= b.size() && b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 0 && b[i + 3] == 1)
        return 4;
    return 0;
}
int nal_type(const std::vector<std::uint8_t>& nal) {
    const auto n = start_code(nal, 0);
    if (!n || n == nal.size() || (nal[n] & 0x80))
        return -1;
    return nal[n] & 31;
}
} // namespace
VideoInfo inspect_video(const std::vector<std::uint8_t>& b) {
    bool aud = false, sps = false, pps = false, idr = false, vcl = false;
    if (nal_type(b) != 9)
        return {};
    for (std::size_t i = 0; i < b.size();) {
        const auto n = start_code(b, i);
        if (!n) {
            ++i;
            continue;
        }
        if (i + n == b.size() || (b[i + n] & 0x80))
            return {};
        const auto type = b[i + n] & 31;
        if (!type || type > 12)
            return {};
        if (type == 9) {
            if (aud)
                return {};
            aud = true;
        }
        sps = sps || type == 7;
        pps = pps || type == 8;
        idr = idr || type == 5;
        vcl = vcl || (type >= 1 && type <= 5);
        i += n + 1;
    }
    return {aud && vcl && (!idr || (sps && pps)), idr && sps && pps};
}
H264FileSource::H264FileSource(const std::string& path) : input_(path, std::ios::binary) {
    if (!input_)
        throw std::runtime_error("cannot open H.264 input: " + path);
}
bool H264FileSource::next_nal(std::vector<std::uint8_t>& nal) {
    if (prefix_.empty()) {
        char c;
        while (input_.get(c)) {
            prefix_.push_back(static_cast<std::uint8_t>(c));
            if (prefix_.size() > 4)
                throw std::runtime_error("input must begin with an Annex B start code");
            if (start_code(prefix_, 0))
                break;
        }
        if (prefix_.empty())
            return false;
        if (!start_code(prefix_, 0))
            throw std::runtime_error("truncated H.264 start code");
    }
    nal = std::move(prefix_);
    prefix_.clear();
    std::size_t zeros = 0;
    char c;
    while (input_.get(c)) {
        const auto byte = static_cast<std::uint8_t>(c);
        nal.push_back(byte);
        if (byte == 1 && zeros >= 2) {
            const auto width = zeros >= 3 ? 4U : 3U;
            prefix_.assign(nal.end() - width, nal.end());
            nal.resize(nal.size() - width);
            break;
        }
        zeros = byte == 0 ? zeros + 1 : 0;
        if (nal.size() > max_unit_size + 4)
            throw std::runtime_error("H.264 NAL exceeds size limit");
    }
    if (input_.bad())
        throw std::runtime_error("H.264 read error");
    if (nal_type(nal) < 0)
        throw std::runtime_error("invalid H.264 NAL");
    return true;
}
bool H264FileSource::next(std::vector<std::uint8_t>& unit) {
    unit.clear();
    std::vector<std::uint8_t> nal;
    if (!pending_.empty()) {
        unit = std::move(pending_);
        pending_.clear();
    } else if (!next_nal(unit))
        return false;
    if (nal_type(unit) != 9)
        throw std::runtime_error("input requires AUD before each access unit (x264 aud=1)");
    while (next_nal(nal)) {
        if (nal_type(nal) == 9) {
            pending_ = std::move(nal);
            break;
        }
        if (unit.size() + nal.size() > max_unit_size)
            throw std::runtime_error("access unit exceeds 1 MiB");
        unit.insert(unit.end(), nal.begin(), nal.end());
    }
    const auto info = inspect_video(unit);
    if (!info.valid || (first_ && !info.recovery_point))
        throw std::runtime_error(
            "input requires valid access units and an initial IDR with SPS/PPS");
    first_ = false;
    return true;
}
std::vector<telemetry::Measurement> UartParser::feed(const std::string& bytes) {
    std::vector<telemetry::Measurement> result;
    for (char c : bytes) {
        if (c != '\n') {
            if (!discard_) {
                if (line_.size() == 80) {
                    line_.clear();
                    discard_ = true;
                } else
                    line_.push_back(c);
            }
            continue;
        }
        bool valid = false;
        telemetry::Measurement m{};
        if (!discard_ && line_.rfind("SIM,", 0) == 0) {
            const auto comma = line_.find(',', 4);
            if (comma != std::string::npos) {
                const char* start = line_.data();
                const char* end = start + line_.size();
                const auto a = std::from_chars(start + 4, start + comma, m.supply_mv);
                const auto b = std::from_chars(start + comma + 1, end, m.temperature_mc);
                valid = a.ec == std::errc{} && a.ptr == start + comma && b.ec == std::errc{} &&
                        b.ptr == end;
            }
        }
        if (valid)
            result.push_back(m);
        else
            ++rejected_;
        line_.clear();
        discard_ = false;
    }
    return result;
}
telemetry::Measurement SimulatedUartSource::sample(std::uint64_t index) {
    const auto m = telemetry::simulated_measurement(index);
    const auto frame =
        "SIM," + std::to_string(m.supply_mv) + "," + std::to_string(m.temperature_mc) + "\n";
    parser_.feed(frame.substr(0, 3)); // Exercise streaming input, not an atomic UART read.
    const auto decoded = parser_.feed(frame.substr(3));
    if (decoded.size() != 1)
        throw std::runtime_error("synthetic UART framing failed");
    return decoded.front();
}
} // namespace simulation
