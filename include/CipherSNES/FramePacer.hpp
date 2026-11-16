// Created by Jacob Hodgkins. CP53: independent wall-clock presentation pacing.
// NEVER changes emulated clocks or skips machine frames; it only decides which
// completed frames the frontend needs to display when recovering from UI stalls.
#pragma once
#include <algorithm>
#include <cstdint>

namespace snes {
class FramePacer final {
public:
    static constexpr std::int64_t NtscPeriodNs = 16639263LL; // 60.0988139 Hz
    static constexpr std::int64_t PalPeriodNs  = 19997209LL; // 50.0069789 Hz
    static constexpr unsigned MaxCatchUp = 3;

    void configure(bool pal, std::int64_t nowNs) {
        periodNs_ = pal ? PalPeriodNs : NtscPeriodNs;
        reset(nowNs);
    }
    void reset(std::int64_t nowNs) {
        nextFrameNs_ = nowNs;
        initialized_ = true;
    }
    // Frames that should be completed this invocation. Bounded catch-up
    // keeps GTK/input/audio responsive under overload and avoids spiral-of-death.
    unsigned framesDue(std::int64_t nowNs) {
        if (!initialized_) reset(nowNs);
        if (nowNs < nextFrameNs_) return 0;
        const std::int64_t maxDebt = static_cast<std::int64_t>(MaxCatchUp-1) * periodNs_;
        if (nowNs - nextFrameNs_ > maxDebt) nextFrameNs_ = nowNs - maxDebt;
        return std::min<unsigned>(MaxCatchUp,
            1u + static_cast<unsigned>((nowNs-nextFrameNs_)/periodNs_));
    }
    void completedFrame() { nextFrameNs_ += periodNs_; }
    std::int64_t nextFrameNs() const { return nextFrameNs_; }
    std::int64_t periodNs() const { return periodNs_; }
private:
    std::int64_t nextFrameNs_=0;
    std::int64_t periodNs_=NtscPeriodNs;
    bool initialized_=false;
};
} // namespace snes
