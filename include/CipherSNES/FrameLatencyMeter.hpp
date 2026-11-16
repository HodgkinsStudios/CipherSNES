// Created by Jacob Hodgkins. CP56: bounded, allocation-free frame-time meter.
// Host-facing diagnostics only. No emulator/savestate dependency.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace snes {
class FrameLatencyMeter final {
public:
    static constexpr unsigned Capacity = 240;
    void reset() { count_ = cursor_ = 0; }
    void addUs(std::uint32_t us) {
        samples_[cursor_] = us;
        cursor_ = (cursor_ + 1u) % Capacity;
        if (count_ < Capacity) ++count_;
    }
    std::uint32_t percentile(unsigned percentage) const {
        if (!count_) return 0;
        // Sorting happens at most once per second in the frontend title.
        std::array<std::uint32_t,Capacity> work = samples_;
        const unsigned pct = std::min(100u,percentage);
        const unsigned rank = std::max(1u, static_cast<unsigned>(
            ((static_cast<std::uint64_t>(count_)*pct)+99u)/100u));
        const unsigned index = std::min(count_-1u, rank-1u);
        std::nth_element(work.begin(),work.begin()+index,work.begin()+count_);
        return work[index];
    }
    unsigned count() const { return count_; }
private:
    std::array<std::uint32_t,Capacity> samples_{};
    unsigned count_=0, cursor_=0;
};
}
