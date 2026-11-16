// CipherSNES CP68: exact integer nearest-neighbour upscaling for the X11
// frontend.  This operates only on already-rendered output pixels; it never
// changes PPU timing, palettes, or the emulated machine state.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace snes {
inline void scaleNearest32(const void* source, std::size_t pitchBytes,
                           unsigned width, unsigned height, unsigned factor,
                           std::uint32_t* destination) {
    if (!source || !destination || !width || !height || !factor) return;
    const std::size_t outWidth = static_cast<std::size_t>(width) * factor;
    const auto* base = static_cast<const std::uint8_t*>(source);
    for (unsigned y = 0; y < height; ++y) {
        const auto* inputRow = reinterpret_cast<const std::uint32_t*>(base + static_cast<std::size_t>(y) * pitchBytes);
        auto* firstRow = destination + static_cast<std::size_t>(y) * factor * outWidth;
        if (factor == 1u) {
            std::memcpy(firstRow, inputRow, outWidth * sizeof(std::uint32_t));
        } else {
            for (unsigned x = 0; x < width; ++x)
                std::fill_n(firstRow + static_cast<std::size_t>(x) * factor, factor, inputRow[x]);
            // Every vertically enlarged row is identical; copy its fully
            // expanded scanline instead of repeating the horizontal work.
            for (unsigned fy = 1; fy < factor; ++fy)
                std::memcpy(firstRow + static_cast<std::size_t>(fy) * outWidth,
                            firstRow, outWidth * sizeof(std::uint32_t));
        }
    }
}
}
