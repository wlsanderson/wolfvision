#pragma once
#include "simulation/protocol.hpp"
#include <functional>
#include <map>
#include <utility>
namespace simulation {
struct VideoStats {
    std::uint64_t written = 0, missing = 0, skipped = 0, invalid = 0, bytes = 0;
    std::size_t peak_bytes = 0, peak_units = 0;
};
class Reassembler {
  public:
    static constexpr std::size_t max_buffer_bytes = std::size_t{4} * 1024 * 1024;
    static constexpr std::size_t max_units = 64;
    using Sink = std::function<void(const std::vector<std::uint8_t>&)>;
    Reassembler(std::uint64_t wait_us, Sink sink) : wait_us_(wait_us), sink_(std::move(sink)) {}
    bool offer(const Packet& packet, std::uint64_t now_us);
    void drain(std::uint64_t now_us, bool force = false);
    void finish(std::uint32_t units, std::uint64_t now_us);
    const VideoStats& stats() const {
        return stats_;
    }
    std::uint64_t next_unit() const {
        return next_;
    }
    std::uint64_t highest_unit_end() const {
        return highest_end_;
    }
    std::size_t buffered_bytes() const {
        return buffered_bytes_;
    }

  private:
    struct Unit {
        std::uint32_t size = 0;
        std::uint64_t since = 0;
        std::size_t bytes = 0, received = 0;
        std::vector<std::vector<std::uint8_t>> fragments;
    };
    void advance(std::uint64_t to);
    void erase(std::map<std::uint32_t, Unit>::iterator it);
    std::map<std::uint32_t, Unit> units_;
    std::uint64_t next_ = 0, highest_end_ = 0, wait_us_;
    std::size_t buffered_bytes_ = 0;
    bool synced_ = false;
    Sink sink_;
    VideoStats stats_;
};
} // namespace simulation
