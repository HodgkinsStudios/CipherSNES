// Created by Jacob Hodgkins. CP56: adaptive, low-overhead GTK wake policy.
// Controls only host event-loop wakeups; never alters emulated master clocks.
#pragma once
#include <algorithm>
#include <cstdint>
namespace snes {
struct FrontendWake {
    static unsigned delayMs(std::int64_t nowNs, std::int64_t nextFrameNs,
                            bool running, bool capturingInput) {
        if (!running) return capturingInput ? 8u : 25u;
        const std::int64_t until = nextFrameNs - nowNs;
        if (until <= 0) return 1u;
        // GLib's millisecond timeouts have no fractional-resolution contract.
        // Round toward the next deadline, rather than burning several 1ms
        // polls per frame. The monotonic frame pacer catches up boundedly on
        // late callbacks. 21ms ceiling covers PAL's 19.997ms period.
        const std::int64_t ms = (until + 999999LL) / 1000000LL;
        return static_cast<unsigned>(std::max<std::int64_t>(1, std::min<std::int64_t>(21, ms)));
    }
};
}
