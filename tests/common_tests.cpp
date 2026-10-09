#include "common/sequence.hpp"
#include <iostream>
#include <stdexcept>
using namespace common;
void check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
int main() {
    try {
        SequenceWindow tracker;
        check(tracker.observe(10).kind == Arrival::first, "first is baseline");
        check(tracker.observe(11).kind == Arrival::in_order, "in order");
        const auto gap = tracker.observe(15);
        check(gap.kind == Arrival::gap && gap.gap == 3, "gap count");
        check(tracker.observe(15).kind == Arrival::duplicate, "duplicate high");
        check(tracker.observe(12).kind == Arrival::out_of_order, "late gap arrival");
        check(tracker.observe(12).kind == Arrival::duplicate, "duplicate late arrival");
        check(tracker.observe(9).kind == Arrival::out_of_order, "earlier than baseline");
        check(tracker.observe(16).kind == Arrival::in_order,
              "late and duplicate preserve watermark");

        SequenceWindow bounds;
        bounds.observe(0);
        const auto large_gap = bounds.observe(UINT64_MAX);
        check(large_gap.kind == Arrival::gap && large_gap.gap == UINT64_MAX - 1,
              "large gap overflow");
        check(bounds.observe(UINT64_MAX).kind == Arrival::duplicate, "max duplicate");
        check(bounds.observe(1).kind == Arrival::stale, "old number outside rolling history");

        SequenceWindow window;
        window.observe(0);
        window.observe(SequenceWindow::capacity);
        check(window.observe(0).kind == Arrival::stale, "lower bound excluded");
        check(window.observe(1).kind == Arrival::out_of_order, "lower bound included");
        check(window.observe(1).kind == Arrival::duplicate, "late duplicate at lower bound");
        for (std::uint64_t i = SequenceWindow::capacity + 1; i < 100000; ++i)
            window.observe(i);
        check(window.size() == SequenceWindow::capacity, "history remains bounded in long stream");
        check(window.observe(1).kind == Arrival::stale, "evicted number is stale");
        std::cout << "Sequence gaps, late/duplicate arrivals, integer boundaries, and bounded "
                     "history passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
