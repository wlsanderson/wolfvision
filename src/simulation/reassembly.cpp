#include "simulation/reassembly.hpp"
#include "simulation/sources.hpp"
#include <algorithm>
namespace simulation {
void Reassembler::erase(std::map<std::uint32_t, Unit>::iterator it) {
    buffered_bytes_ -= it->second.bytes;
    units_.erase(it);
}
void Reassembler::advance(std::uint64_t to) {
    if (to <= next_)
        return;
    stats_.missing += to - next_;
    next_ = to;
    synced_ = false;
    while (!units_.empty() && units_.begin()->first < next_)
        erase(units_.begin());
}
bool Reassembler::offer(const Packet& p, std::uint64_t now) {
    if (p.type != Type::video || p.unit == UINT32_MAX || !p.total_size ||
        p.total_size > max_unit_size ||
        p.fragments != (p.total_size + fragment_size - 1) / fragment_size ||
        p.fragment >= p.fragments ||
        p.payload.size() !=
            std::min<std::size_t>(fragment_size, p.total_size - p.fragment * fragment_size))
        return false;
    if (p.unit < next_)
        return true; // Already emitted/expired.
    highest_end_ = std::max(highest_end_, static_cast<std::uint64_t>(p.unit) + 1);
    if (static_cast<std::uint64_t>(p.unit) >= next_ + max_units)
        advance(static_cast<std::uint64_t>(p.unit) - max_units + 1);
    auto found = units_.find(p.unit);
    if (found != units_.end()) {
        auto& u = found->second;
        if (u.size != p.total_size || u.fragments.size() != p.fragments)
            return false;
        if (!u.fragments[p.fragment].empty())
            return u.fragments[p.fragment] == p.payload;
    }
    while (buffered_bytes_ + p.payload.size() > max_buffer_bytes && !units_.empty()) {
        advance(static_cast<std::uint64_t>(units_.begin()->first) + 1);
    }
    if (p.unit < next_)
        return true;
    auto [it, inserted] = units_.try_emplace(p.unit);
    auto& u = it->second;
    if (inserted) {
        u.size = p.total_size;
        u.since = now;
        u.fragments.resize(p.fragments);
    }
    u.fragments[p.fragment] = p.payload;
    ++u.received;
    u.bytes += p.payload.size();
    buffered_bytes_ += p.payload.size();
    stats_.peak_bytes = std::max(stats_.peak_bytes, buffered_bytes_);
    stats_.peak_units = std::max(stats_.peak_units, units_.size());
    drain(now);
    return true;
}
void Reassembler::drain(std::uint64_t now, bool force) {
    while (!units_.empty()) {
        auto it = units_.begin();
        auto& u = it->second;
        const bool expired = force || (now >= u.since && now - u.since >= wait_us_);
        if (it->first != next_) {
            if (!expired)
                break;
            advance(it->first);
            continue;
        }
        if (u.received != u.fragments.size()) {
            if (!expired)
                break;
            advance(next_ + 1);
            continue;
        }
        std::vector<std::uint8_t> bytes;
        bytes.reserve(u.size);
        for (const auto& fragment : u.fragments)
            bytes.insert(bytes.end(), fragment.begin(), fragment.end());
        const auto info = inspect_video(bytes);
        if (!info.valid) {
            ++stats_.invalid;
            synced_ = false;
        } else if (info.recovery_point || synced_) {
            synced_ = true;
            sink_(bytes);
            ++stats_.written;
            stats_.bytes += bytes.size();
        } else
            ++stats_.skipped;
        ++next_;
        erase(it);
    }
}
void Reassembler::finish(std::uint32_t units, std::uint64_t now) {
    drain(now, true);
    advance(units);
}
} // namespace simulation
