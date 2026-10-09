#pragma once
#include <cstddef>
#include <cstdint>
#include <set>
namespace common {
enum class Arrival : std::uint8_t { first, in_order, gap, duplicate, out_of_order, stale };
struct ArrivalResult {
    Arrival kind;
    std::uint64_t gap = 0;
};
class SequenceWindow {
  public:
    // Remember only [highest - capacity + 1, highest]. Older arrivals are stale.
    static constexpr std::uint64_t capacity = 4096;
    ArrivalResult observe(std::uint64_t sequence) {
        if (seen_.empty()) {
            highest_ = sequence;
            seen_.insert(sequence);
            return {Arrival::first};
        }
        if (sequence <= highest_ && highest_ - sequence >= capacity)
            return {Arrival::stale};
        if (!seen_.insert(sequence).second)
            return {Arrival::duplicate};
        if (sequence < highest_)
            return {Arrival::out_of_order};
        const auto gap = sequence - highest_ - 1;
        highest_ = sequence;
        if (highest_ >= capacity)
            seen_.erase(seen_.begin(), seen_.upper_bound(highest_ - capacity));
        return {gap ? Arrival::gap : Arrival::in_order, gap};
    }
    std::size_t size() const {
        return seen_.size();
    }

  private:
    std::set<std::uint64_t> seen_;
    std::uint64_t highest_ = 0;
};
inline const char* arrival_name(Arrival kind) {
    switch (kind) {
    case Arrival::first:
        return "first";
    case Arrival::in_order:
        return "in_order";
    case Arrival::gap:
        return "gap";
    case Arrival::duplicate:
        return "duplicate";
    case Arrival::out_of_order:
        return "out_of_order";
    case Arrival::stale:
        return "stale";
    }
    return "unknown";
}
} // namespace common
