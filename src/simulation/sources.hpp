#pragma once
#include "telemetry/protocol.hpp"
#include <fstream>
#include <string>
#include <vector>
namespace simulation {
struct VideoInfo {
    bool valid = false, recovery_point = false;
};
VideoInfo inspect_video(const std::vector<std::uint8_t>& bytes);
// One AUD-delimited Annex B access unit at a time; never loads the entire file.
class H264FileSource {
  public:
    explicit H264FileSource(const std::string& path);
    bool next(std::vector<std::uint8_t>& unit);

  private:
    bool next_nal(std::vector<std::uint8_t>& nal);
    std::ifstream input_;
    std::vector<std::uint8_t> prefix_, pending_;
    bool first_ = true;
};
// Synthetic UART framing for this milestone only: SIM,<mv>,<mC>\n.
class UartParser {
  public:
    std::vector<telemetry::Measurement> feed(const std::string& bytes);
    std::size_t rejected() const {
        return rejected_;
    }

  private:
    std::string line_;
    bool discard_ = false;
    std::size_t rejected_ = 0;
};
class SimulatedUartSource {
  public:
    telemetry::Measurement sample(std::uint64_t index);

  private:
    UartParser parser_;
};
} // namespace simulation
