#include "snes/core/PPU.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace snes {

PPU::PPU()
    : frameBuffer_(FrameWidth * FrameHeight, 0xFF000000u),
      layerBuffer_(FrameWidth * FrameHeight, 5u), mainPixels_(), subPixels_(),
      vram_(64u * KiB, 0), cgram_(512u, 0), oam_(544u, 0),
      interlaceFrameScratch_(), interlaceLayerScratch_(),
      tileCache2_(4096u * 64u, 0), tileCache4_(2048u * 64u, 0), tileCache8_(1024u * 64u, 0),
      tileCache2Valid_(4096u, 0), tileCache4Valid_(2048u, 0), tileCache8Valid_(1024u, 0),
      colorLut_(32768u, 0xFF000000u), backgrounds_(),
      outputWidth_(FrameWidth), outputHeight_(FrameHeight), renderWidth_(FrameWidth),
      scanline_(0), dot_(0), masterClock_(0), lineMasterClocks_(1364),
      totalScanlines_(262), frameNumber_(0), pal_(false), field_(false),
      vblank_(false), frameReady_(false), hblankStartEvent_(false), hdmaStartEvent_(false),
      vblankStartEvent_(false), frameStartEvent_(false), refreshStartEvent_(false),
      rangeOver_(false), timeOver_(false), renderFrameEnabled_(true),
      frameSurfacePrepared_(false), frameHadHires_(false), renderedPixelsThisLine_(0),
      blockedVramAccesses_(0), blockedCgramAccesses_(0), blockedOamAccesses_(0),
      objFetchBlockedCurrent_(0), objFetchBlockedPending_(0),
      displayControl_(0x80u), objectSelection_(0), bgMode_(0), mosaic_(0),
      mainScreen_(0), subScreen_(0), setIni_(0), vmain_(0), vramAddress_(0),
      vramReadBuffer_(0), oamReloadAddress_(0), oamAddress_(0), oamWriteLatch_(0), oamWriteHigh_(false),
      oamPriorityRotation_(false), oamFirstSprite_(0),
      cgramAddress_(0), cgramHigh_(false), cgramLatch_(0), scrollLatch_(0),
      horizontalScrollLatch_(0), hCounterLatch_(0), vCounterLatch_(0),
      hCounterHigh_(false), vCounterHigh_(false), mode7Control_(0),
      mode7A_(0x0100), mode7B_(0), mode7C_(0), mode7D_(0x0100),
      mode7X_(0), mode7Y_(0), mode7HOffset_(0), mode7VOffset_(0), mode7Latch_(0), mode7SecondWrite_(false),
      ppu1OpenBus_(0), ppu2OpenBus_(0), countersLatched_(false), counterLatchEnabled_(true),
      counterLatchPending_(false), pendingCounterH_(0), pendingCounterV_(0), pendingCounterFrame_(0),
      windowSelect_(), windowLeft_(), windowRight_(), windowLogic_(),
      mainWindowMask_(0), subWindowMask_(0), colorMathControl_(0),
      colorMathEnable_(0), fixedColor_(0) {
    reset();
}

void PPU::reset() {
    outputWidth_ = FrameWidth;
    outputHeight_ = FrameHeight;
    renderWidth_ = FrameWidth;
    frameBuffer_.assign(FrameWidth * FrameHeight, 0xFF000000u);
    layerBuffer_.assign(FrameWidth * FrameHeight, 5u);
    mainPixels_.clear();
    subPixels_.clear();
    interlaceFrameScratch_.clear();
    interlaceLayerScratch_.clear();
    std::fill(tileCache2Valid_.begin(), tileCache2Valid_.end(), 0);
    std::fill(tileCache4Valid_.begin(), tileCache4Valid_.end(), 0);
    std::fill(tileCache8Valid_.begin(), tileCache8Valid_.end(), 0);
    std::fill(vram_.begin(), vram_.end(), 0);
    std::fill(cgram_.begin(), cgram_.end(), 0);
    std::fill(oam_.begin(), oam_.end(), 0);
    for (unsigned i = 0; i < 4; ++i) backgrounds_[i] = Background();
    scanline_ = 0;
    dot_ = 0;
    masterClock_ = 0;
    field_ = false;
    totalScanlines_ = pal_ ? 312 : 262;
    lineMasterClocks_ = currentLineMasterClocks();
    frameNumber_ = 0;
    vblank_ = false;
    frameReady_ = false;
    hblankStartEvent_ = false;
    hdmaStartEvent_ = false;
    vblankStartEvent_ = false;
    frameStartEvent_ = false;
    refreshStartEvent_ = false;
    rangeOver_ = false;
    timeOver_ = false;
    renderFrameEnabled_ = true;
    frameSurfacePrepared_ = false;
    frameHadHires_ = false;
    renderedPixelsThisLine_ = 0;
    blockedVramAccesses_ = 0;
    blockedCgramAccesses_ = 0;
    blockedOamAccesses_ = 0;
    objFetchBlockedCurrent_ = 0;
    objFetchBlockedPending_ = 0;
    displayControl_ = 0x80u;
    objectSelection_ = 0;
    bgMode_ = 0;
    mosaic_ = 0;
    mainScreen_ = 0;
    subScreen_ = 0;
    setIni_ = 0;
    vmain_ = 0;
    vramAddress_ = 0;
    vramReadBuffer_ = 0;
    oamReloadAddress_ = 0;
    oamAddress_ = 0;
    oamWriteLatch_ = 0;
    oamWriteHigh_ = false;
    oamPriorityRotation_ = false;
    oamFirstSprite_ = 0;
    cgramAddress_ = 0;
    cgramHigh_ = false;
    cgramLatch_ = 0;
    scrollLatch_ = 0;
    horizontalScrollLatch_ = 0;
    hCounterLatch_ = 0;
    vCounterLatch_ = 0;
    hCounterHigh_ = false;
    vCounterHigh_ = false;
    mode7Control_ = 0;
    mode7A_ = 0x0100;
    mode7B_ = 0;
    mode7C_ = 0;
    mode7D_ = 0x0100;
    mode7X_ = 0;
    mode7Y_ = 0;
    mode7Latch_ = 0;
    mode7SecondWrite_ = false;
    ppu1OpenBus_ = 0;
    ppu2OpenBus_ = 0;
    countersLatched_ = false;
    counterLatchEnabled_ = true;
    counterLatchPending_ = false;
    pendingCounterH_ = 0;
    pendingCounterV_ = 0;
    pendingCounterFrame_ = 0;
    for (unsigned i = 0; i < 3; ++i) windowSelect_[i] = 0;
    for (unsigned i = 0; i < 2; ++i) {
        windowLeft_[i] = 0;
        windowRight_[i] = 0;
        windowLogic_[i] = 0;
    }
    mainWindowMask_ = 0;
    subWindowMask_ = 0;
    colorMathControl_ = 0;
    colorMathEnable_ = 0;
    fixedColor_ = 0;
    rebuildColorLut();
}

void PPU::setPal(bool pal) {
    pal_ = pal;
    const int base = pal_ ? 312 : 262;
    totalScanlines_ = base + (((setIni_ & 1u) && !field_) ? 1 : 0);
    if (scanline_ >= totalScanlines_) scanline_ = 0;
    lineMasterClocks_ = currentLineMasterClocks();
}

void PPU::latchCounters(int horizontal, int vertical) {
    hCounterLatch_ = static_cast<u16>(std::max(0, std::min(340, horizontal)));
    vCounterLatch_ = static_cast<u16>(std::max(0, std::min(totalScanlines_ - 1, vertical)));
    countersLatched_ = true;
}

// On a WRIO ($4201.7) falling edge the external latch samples one PPU
// dot later than a software read from SLHV ($2137) in the equivalent bus
// slot (Anomie S-CPU timing notes). Handle line/frame rollover here, not by
// clamping dot 340 to the previous scanline.
void PPU::latchCountersFromWrioFallingEdge() {
    if (dot_ >= 339) {
        latchCounters(0, (scanline_ + 1) % totalScanlines_);
    } else {
        latchCounters(dot_ + 1, scanline_);
    }
}

void PPU::scheduleCounterLatch(int horizontal, int vertical) {
    pendingCounterH_ = static_cast<u16>(std::max(0, std::min(340, horizontal)));
    pendingCounterV_ = static_cast<u16>(std::max(0, std::min(totalScanlines_ - 1, vertical)));
    pendingCounterFrame_ = frameNumber_;
    if (scanline_ > static_cast<int>(pendingCounterV_) ||
        (scanline_ == static_cast<int>(pendingCounterV_) && dot_ >= static_cast<int>(pendingCounterH_)))
        ++pendingCounterFrame_;
    counterLatchPending_ = true;
}

void PPU::setCounterLatchEnabled(bool enabled) { counterLatchEnabled_ = enabled; }

int PPU::visibleLineCount() const {
    return (setIni_ & 0x04u) ? OverscanHeight : FrameHeight;
}

int PPU::currentLineMasterClocks() const {
    const bool interlace = (setIni_ & 0x01u) != 0;
    if (!pal_ && !interlace && field_ && scanline_ == 240) return 1360;
    if (pal_ && interlace && field_ && scanline_ == 311) return 1368;
    return 1364;
}

void PPU::updateDotFromMasterClock() {
    int adjusted = masterClock_;
    // On normal/long lines, dots 323 (clocks 1292..1297) and 327
    // (1310..1315) consume six master clocks.  Account for the extra
    // pair at 1294 and 1312, rather than advancing the observed H dot
    // at the ordinary four-clock boundary.  These thresholds are kept
    // even-clock aligned so one-clock stepping remains monotonic.
    if (lineMasterClocks_ != 1360) {
        if (adjusted >= 1294) adjusted -= 2;
        if (adjusted >= 1312) adjusted -= 2;
    }
    dot_ = std::max(0, std::min(340, adjusted / 4));
}

void PPU::advanceScanline() {
    // The character fetches just completed during HBlank supply the NEXT
    // scanline. Drawing from current OAM/VRAM alone misses this dependency.
    objFetchBlockedCurrent_ = objFetchBlockedPending_;
    ++scanline_;
    const int vblankStart = visibleLineCount() + 1;
    if (scanline_ == vblankStart) {
        vblank_ = true;
        vblankStartEvent_ = true;
        frameReady_ = true;
        if ((displayControl_ & 0x80u) == 0u) {
            oamAddress_ = static_cast<u16>((oamReloadAddress_ << 1) & 0x03FFu);
            if (oamPriorityRotation_)
                oamFirstSprite_ = static_cast<u8>((oamAddress_ >> 2) & 0x7Fu);
            oamWriteHigh_ = false;
        }
    }
    if (scanline_ >= totalScanlines_) {
        scanline_ = 0;
        vblank_ = false;
        rangeOver_ = false;
        timeOver_ = false;
        field_ = !field_;
        const int base = pal_ ? 312 : 262;
        totalScanlines_ = base + (((setIni_ & 1u) && !field_) ? 1 : 0);
        ++frameNumber_;
        frameStartEvent_ = true;
        frameSurfacePrepared_ = false;
        frameHadHires_ = false;
    }
    renderedPixelsThisLine_ = 0;
    lineMasterClocks_ = currentLineMasterClocks();
}

void PPU::stepMasterClocks(unsigned clocks) {
    // CP55: CPU bus accesses ordinarily fit wholly between PPU events.
    // Advance those intervals in one update, leaving all edge-triggered paths
    // (including counter latches, refresh, HBlank/HDMA, and line rollover)
    // in the original event loop below. The strict < preserves event timing.
    if (clocks != 0u && !counterLatchPending_) {
        const int nextEvent = masterClock_ < 538 ? 538 :
                              masterClock_ < 1096 ? 1096 :
                              masterClock_ < 1104 ? 1104 : lineMasterClocks_;
        if (static_cast<unsigned>(masterClock_) + clocks < static_cast<unsigned>(nextEvent)) {
            masterClock_ += static_cast<int>(clocks);
            updateDotFromMasterClock();
            return;
        }
    }
    while (clocks != 0u) {
        if (counterLatchPending_ && frameNumber_ == pendingCounterFrame_ &&
            scanline_ == static_cast<int>(pendingCounterV_) && dot_ >= static_cast<int>(pendingCounterH_)) {
            latchCounters(pendingCounterH_, pendingCounterV_);
            counterLatchPending_ = false;
        }
        int nextEvent = lineMasterClocks_;
        if (masterClock_ < 538) nextEvent = std::min(nextEvent, 538);
        if (masterClock_ < 1096) nextEvent = std::min(nextEvent, 1096);
        if (masterClock_ < 1104) nextEvent = std::min(nextEvent, 1104);
        if (counterLatchPending_ && frameNumber_ == pendingCounterFrame_ &&
            scanline_ == static_cast<int>(pendingCounterV_)) {
            const int latchClock = static_cast<int>(pendingCounterH_) * 4;
            if (masterClock_ < latchClock) nextEvent = std::min(nextEvent, latchClock);
        }
        const unsigned distance = static_cast<unsigned>(nextEvent - masterClock_);
        const unsigned advance = distance == 0u ? 1u : std::min(clocks, distance);
        masterClock_ += static_cast<int>(advance);
        clocks -= advance;
        updateDotFromMasterClock();

        if (counterLatchPending_ && frameNumber_ == pendingCounterFrame_ &&
            scanline_ == static_cast<int>(pendingCounterV_) &&
            dot_ >= static_cast<int>(pendingCounterH_)) {
            latchCounters(pendingCounterH_, pendingCounterV_);
            counterLatchPending_ = false;
        }

        if (masterClock_ == 538) refreshStartEvent_ = true;
        if (masterClock_ == 1096) {
            hblankStartEvent_ = true;
            // 34 OBJ tile-fetch slots occupy late HBlank. The effective
            // data availability for the next line is sampled per slot.
            objFetchBlockedPending_ = forcedBlank() ? ((1ull << 34) - 1ull) : 0ull;
            // Active display is lines 1..224 (or 1..239 in overscan).  Render
            // the line at the HBlank edge while all registers and memories
            // still contain the values that were visible on that line.  This
            // fixes the common real-game pattern of asserting forced blank in
            // VBlank after a perfectly visible frame.
            if (renderFrameEnabled_ && scanline_ >= 1 && scanline_ <= visibleLineCount()) {
                renderScanlineSegment(static_cast<unsigned>(scanline_ - 1),
                                      renderedPixelsThisLine_, FrameWidth);
                renderedPixelsThisLine_ = FrameWidth;
            }
        }
        if (masterClock_ == 1104) hdmaStartEvent_ = true;
        if (masterClock_ >= lineMasterClocks_) {
            masterClock_ = 0;
            dot_ = 0;
            advanceScanline();
        }
    }
}

void PPU::stepDot() {
    const int target = std::min(lineMasterClocks_, masterClock_ + 4);
    stepMasterClocks(static_cast<unsigned>(target - masterClock_));
}

void PPU::stepDots(unsigned dots) {
    // Compatibility entry point for tests and older callers.  The scheduler
    // uses master clocks directly so long dots and special scanlines remain
    // exact; this method advances the nominal four clocks per requested dot.
    stepMasterClocks(dots * 4u);
}

u16 PPU::translatedVramAddress() const {
    const u16 address = vramAddress_;
    switch ((vmain_ >> 2) & 3u) {
        case 1:
            return static_cast<u16>((address & 0xFF00u) | ((address & 0x001Fu) << 3) |
                                    ((address & 0x00E0u) >> 5));
        case 2:
            return static_cast<u16>((address & 0xFE00u) | ((address & 0x003Fu) << 3) |
                                    ((address & 0x01C0u) >> 6));
        case 3:
            return static_cast<u16>((address & 0xFC00u) | ((address & 0x007Fu) << 3) |
                                    ((address & 0x0380u) >> 7));
        default:
            return address;
    }
}

void PPU::incrementVramAddress(bool highPort) {
    const bool incrementAfterHigh = (vmain_ & 0x80u) != 0;
    if (incrementAfterHigh != highPort) return;
    static const unsigned increments[4] = { 1u, 32u, 128u, 128u };
    vramAddress_ = static_cast<u16>(vramAddress_ + increments[vmain_ & 3u]);
}

void PPU::prefetchVramReadBuffer() {
    if (!videoMemoryAccessible()) {
        ++blockedVramAccesses_;
        return;
    }
    const std::size_t byteAddress = (static_cast<std::size_t>(translatedVramAddress()) << 1) & 0xFFFFu;
    vramReadBuffer_ = static_cast<u16>(vram_[byteAddress] |
        (static_cast<u16>(vram_[(byteAddress + 1u) & 0xFFFFu]) << 8));
}

bool PPU::videoMemoryAccessible() const {
    // Hardware-derived documentation agrees on the stable software-visible
    // rule: VRAM/OAM/CGRAM are freely accessible in VBlank or forced blank.
    // HBlank contains only narrow, model-sensitive conflict slots, so treating
    // all of HBlank as safe creates deterministic but incorrect palette/tile
    // writes.  Those fragile conflict artifacts remain outside this renderer.
    return vblank_ || forcedBlank();
}

void PPU::writeRegister(u16 address, u8 value) {
    // Preserve the part of the active scanline that has already reached the
    // video output before a raster-time register or memory write changes PPU
    // state.  The new value therefore affects only subsequent pixels.
    flushActiveDisplayToCurrentPosition();
    ppu1OpenBus_ = value;
    switch (address) {
        case 0x2100: {
            const bool wasForcedBlank = (displayControl_ & 0x80u) != 0;
            // A mid-HBlank display-enable change affects subsequent sprite
            // tile-fetch slots. Do not retroactively modify completed slots.
            if (masterClock_ >= 1096 && masterClock_ < lineMasterClocks_) {
                const unsigned slot = std::min(34u, static_cast<unsigned>((masterClock_ - 1096) / 8));
                const u64 doneMask = (1ull << slot) - 1ull;
                const u64 remainingMask = ((1ull << 34) - 1ull) & ~doneMask;
                // A forced-blank edge aborts the current OBJ tile-fetch
                // sequence. Clearing INIDISP later in the SAME HBlank does
                // not restart the abandoned sequence; allow the next HBlank
                // to begin a fresh sequence instead.
                if ((value & 0x80u) != 0u)
                    objFetchBlockedPending_ |= remainingMask;
                (void)doneMask;
            }
            if ((displayControl_ & 0x0Fu) != (value & 0x0Fu)) {
                displayControl_ = value;
                rebuildColorLut();
            } else displayControl_ = value;
            // The PPU reloads the internal OAM byte address when forced blank
            // is released during the first VBlank scanline.  Many games rely
            // on this before beginning an OAM DMA.
            const int firstVBlankLine = visibleLineCount() + 1;
            if (wasForcedBlank && (value & 0x80u) == 0 && scanline_ == firstVBlankLine) {
                oamAddress_ = static_cast<u16>((oamReloadAddress_ << 1) & 0x03FFu);
                if (oamPriorityRotation_)
                    oamFirstSprite_ = static_cast<u8>((oamAddress_ >> 2) & 0x7Fu);
                oamWriteHigh_ = false;
            }
            break;
        }
        case 0x2101: objectSelection_ = value; break;
        case 0x2102:
            oamReloadAddress_ = static_cast<u16>((oamReloadAddress_ & 0x100u) | value);
            oamAddress_ = static_cast<u16>((oamReloadAddress_ << 1) & 0x03FFu);
            if (oamPriorityRotation_)
                oamFirstSprite_ = static_cast<u8>((oamAddress_ >> 2) & 0x7Fu);
            oamWriteHigh_ = false;
            break;
        case 0x2103:
            oamReloadAddress_ = static_cast<u16>((oamReloadAddress_ & 0x0FFu) | ((value & 1u) << 8));
            oamAddress_ = static_cast<u16>((oamReloadAddress_ << 1) & 0x03FFu);
            oamPriorityRotation_ = (value & 0x80u) != 0;
            oamFirstSprite_ = oamPriorityRotation_ ?
                static_cast<u8>((oamAddress_ >> 2) & 0x7Fu) : 0u;
            oamWriteHigh_ = false;
            break;
        case 0x2104: {
            // OAM uses a 10-bit internal byte address.  Low-table writes are
            // committed only when the odd byte is written; high-table writes
            // are immediate and $220-$3ff mirror $200-$21f.
            const bool accessible = videoMemoryAccessible();
            if (!accessible) ++blockedOamAccesses_;
            if (oamAddress_ < 0x0200u) {
                if ((oamAddress_ & 1u) == 0u) {
                    oamWriteLatch_ = value;
                } else if (accessible) {
                    oam_[oamAddress_ - 1u] = oamWriteLatch_;
                    oam_[oamAddress_] = value;
                }
            } else if (accessible) {
                oam_[0x0200u + ((oamAddress_ - 0x0200u) & 0x1Fu)] = value;
            }
            oamAddress_ = static_cast<u16>((oamAddress_ + 1u) & 0x03FFu);
            if (oamPriorityRotation_)
                oamFirstSprite_ = static_cast<u8>((oamAddress_ >> 2) & 0x7Fu);
            oamWriteHigh_ = (oamAddress_ & 1u) != 0u;
            break;
        }
        case 0x2105: bgMode_ = value; break;
        case 0x2106: mosaic_ = value; break;
        case 0x2107: backgrounds_[0].screenBaseAndSize = value; break;
        case 0x2108: backgrounds_[1].screenBaseAndSize = value; break;
        case 0x2109: backgrounds_[2].screenBaseAndSize = value; break;
        case 0x210A: backgrounds_[3].screenBaseAndSize = value; break;
        case 0x210B:
            backgrounds_[0].tileBase = static_cast<u16>((value & 0x0Fu) << 13);
            backgrounds_[1].tileBase = static_cast<u16>((value >> 4) << 13);
            break;
        case 0x210C:
            backgrounds_[2].tileBase = static_cast<u16>((value & 0x0Fu) << 13);
            backgrounds_[3].tileBase = static_cast<u16>((value >> 4) << 13);
            break;
        case 0x210D: case 0x210E: case 0x210F: case 0x2110:
        case 0x2111: case 0x2112: case 0x2113: case 0x2114: {
            const unsigned reg = static_cast<unsigned>(address - 0x210Du);
            const unsigned bg = reg / 2u;
            const bool vertical = (reg & 1u) != 0;
            u16& target = vertical ? backgrounds_[bg].vOffset : backgrounds_[bg].hOffset;
            if (vertical) {
                // BGnVOFS = (value << 8) | shared BG offset latch.
                target = static_cast<u16>(((static_cast<u16>(value) << 8) | scrollLatch_) & 0x03FFu);
            } else {
                // The horizontal registers use both PPU scroll latches:
                // BGnHOFS = (value << 8) | (bgofs_latch & ~7) |
                //            (bghofs_latch & 7).
                target = static_cast<u16>(((static_cast<u16>(value) << 8) |
                    (scrollLatch_ & 0xF8u) | (horizontalScrollLatch_ & 7u)) & 0x03FFu);
                horizontalScrollLatch_ = value;
            }
            // Mode 7 H/V offsets share a separate previous-byte latch and
            // update on every write, even though software conventionally
            // writes low then high.
            if (address == 0x210Du || address == 0x210Eu) {
                const u16 raw = static_cast<u16>((static_cast<u16>(value) << 8) | mode7Latch_);
                const s16 signed13 = static_cast<s16>((raw & 0x1000u) ? (raw | 0xE000u) : (raw & 0x1FFFu));
                if (address == 0x210Du) mode7HOffset_ = signed13;
                else mode7VOffset_ = signed13;
                mode7Latch_ = value;
            }
            scrollLatch_ = value;
            break;
        }
        case 0x2115: vmain_ = value; break;
        case 0x2116:
            vramAddress_ = static_cast<u16>((vramAddress_ & 0xFF00u) | value);
            // VMADDL writes reload the VRAM read latch just as VMADDH
            // writes do. The latch may be accessed through $2139/$213A.
            prefetchVramReadBuffer();
            break;
        case 0x2117:
            vramAddress_ = static_cast<u16>((vramAddress_ & 0x00FFu) | (static_cast<u16>(value) << 8));
            prefetchVramReadBuffer();
            break;
        case 0x2118: {
            const std::size_t byteAddress = (static_cast<std::size_t>(translatedVramAddress()) << 1) & 0xFFFFu;
            if (videoMemoryAccessible()) {
                vram_[byteAddress] = value;
                invalidateTileCache(byteAddress);
            } else ++blockedVramAccesses_;
            incrementVramAddress(false);
            break;
        }
        case 0x2119: {
            const std::size_t byteAddress = ((static_cast<std::size_t>(translatedVramAddress()) << 1) + 1u) & 0xFFFFu;
            if (videoMemoryAccessible()) {
                vram_[byteAddress] = value;
                invalidateTileCache(byteAddress);
            } else ++blockedVramAccesses_;
            incrementVramAddress(true);
            break;
        }
        case 0x211A: mode7Control_ = value; break;
        case 0x211B: case 0x211C: case 0x211D: case 0x211E:
        case 0x211F: case 0x2120: {
            s16* target = 0;
            if (address == 0x211B) target = &mode7A_;
            else if (address == 0x211C) target = &mode7B_;
            else if (address == 0x211D) target = &mode7C_;
            else if (address == 0x211E) target = &mode7D_;
            else if (address == 0x211F) target = &mode7X_;
            else target = &mode7Y_;
            const u16 raw = static_cast<u16>(mode7Latch_ | (static_cast<u16>(value) << 8));
            if (address == 0x211Fu || address == 0x2120u)
                *target = static_cast<s16>((raw & 0x1000u) ? (raw | 0xE000u) : (raw & 0x1FFFu));
            else
                *target = static_cast<s16>(raw);
            mode7Latch_ = value;
            mode7SecondWrite_ = false;
            break;
        }
        case 0x2121:
            cgramAddress_ = value;
            cgramHigh_ = false;
            break;
        case 0x2122:
            if (!cgramHigh_) {
                cgramLatch_ = value;
                cgramHigh_ = true;
            } else {
                const std::size_t index = static_cast<std::size_t>(cgramAddress_) * 2u;
                if (videoMemoryAccessible()) {
                    cgram_[index] = cgramLatch_;
                    cgram_[index + 1u] = static_cast<u8>(value & 0x7Fu);
                } else ++blockedCgramAccesses_;
                ++cgramAddress_;
                cgramHigh_ = false;
            }
            break;
        case 0x2123: windowSelect_[0] = value; break;
        case 0x2124: windowSelect_[1] = value; break;
        case 0x2125: windowSelect_[2] = value; break;
        case 0x2126: windowLeft_[0] = value; break;
        case 0x2127: windowRight_[0] = value; break;
        case 0x2128: windowLeft_[1] = value; break;
        case 0x2129: windowRight_[1] = value; break;
        case 0x212A: windowLogic_[0] = value; break;
        case 0x212B: windowLogic_[1] = value; break;
        case 0x212C: mainScreen_ = value; break;
        case 0x212D: subScreen_ = value; break;
        case 0x212E: mainWindowMask_ = value; break;
        case 0x212F: subWindowMask_ = value; break;
        case 0x2130: colorMathControl_ = value; break;
        case 0x2131: colorMathEnable_ = value; break;
        case 0x2132: {
            const u16 component = static_cast<u16>(value & 0x1Fu);
            if (value & 0x20u) fixedColor_ = static_cast<u16>((fixedColor_ & ~0x001Fu) | component);
            if (value & 0x40u) fixedColor_ = static_cast<u16>((fixedColor_ & ~0x03E0u) | (component << 5));
            if (value & 0x80u) fixedColor_ = static_cast<u16>((fixedColor_ & ~0x7C00u) | (component << 10));
            break;
        }
        case 0x2133:
            setIni_ = value;
            totalScanlines_ = (pal_ ? 312 : 262) + (((setIni_ & 1u) && !field_) ? 1 : 0);
            lineMasterClocks_ = currentLineMasterClocks();
            break;
        default: break;
    }
}

u8 PPU::readRegister(u16 address) {
    switch (address) {
        case 0x2134: case 0x2135: case 0x2136: {
            const s32 product = static_cast<s32>(mode7A_) * static_cast<s8>(mode7B_ >> 8);
            return static_cast<u8>((static_cast<u32>(product) >> ((address - 0x2134u) * 8u)) & 0xFFu);
        }
        case 0x2137:
            if (counterLatchEnabled_) latchCounters(dot_, scanline_);
            return ppu1OpenBus_;
        case 0x2138: {
            const std::size_t byte = oamAddress_ < 0x0200u ?
                static_cast<std::size_t>(oamAddress_) :
                static_cast<std::size_t>(0x0200u + ((oamAddress_ - 0x0200u) & 0x1Fu));
            const bool accessible = videoMemoryAccessible();
            const u8 value = accessible ? oam_[byte] : ppu1OpenBus_;
            if (!accessible) ++blockedOamAccesses_;
            oamAddress_ = static_cast<u16>((oamAddress_ + 1u) & 0x03FFu);
            if (oamPriorityRotation_)
                oamFirstSprite_ = static_cast<u8>((oamAddress_ >> 2) & 0x7Fu);
            oamWriteHigh_ = (oamAddress_ & 1u) != 0u;
            ppu1OpenBus_ = value;
            return value;
        }
        case 0x2139: {
            const bool accessible = videoMemoryAccessible();
            const u8 value = accessible ? static_cast<u8>(vramReadBuffer_) : ppu1OpenBus_;
            if (!accessible) ++blockedVramAccesses_;
            // VRAM read-port access prefetches the word at the *current*
            // address before the VMADD counter advances. Fetching after
            // the increment skips a word (observable by $213A dummy reads).
            if ((vmain_ & 0x80u) == 0u) prefetchVramReadBuffer();
            incrementVramAddress(false);
            ppu1OpenBus_ = value;
            return value;
        }
        case 0x213A: {
            const bool accessible = videoMemoryAccessible();
            const u8 value = accessible ? static_cast<u8>(vramReadBuffer_ >> 8) : ppu1OpenBus_;
            if (!accessible) ++blockedVramAccesses_;
            if ((vmain_ & 0x80u) != 0u) prefetchVramReadBuffer();
            incrementVramAddress(true);
            ppu1OpenBus_ = value;
            return value;
        }
        case 0x213B: {
            const std::size_t index = static_cast<std::size_t>(cgramAddress_) * 2u + (cgramHigh_ ? 1u : 0u);
            const bool accessible = videoMemoryAccessible();
            u8 value = accessible ? cgram_[index & 0x1FFu] : ppu2OpenBus_;
            if (!accessible) ++blockedCgramAccesses_;
            if (cgramHigh_) value = static_cast<u8>((value & 0x7Fu) | (ppu2OpenBus_ & 0x80u));
            cgramHigh_ = !cgramHigh_;
            if (!cgramHigh_) ++cgramAddress_;
            ppu2OpenBus_ = value;
            return value;
        }
        case 0x213C: {
            const u8 value = !hCounterHigh_ ? static_cast<u8>(hCounterLatch_) :
                static_cast<u8>((ppu2OpenBus_ & 0xFEu) | ((hCounterLatch_ >> 8) & 1u));
            hCounterHigh_ = !hCounterHigh_;
            ppu2OpenBus_ = value;
            return value;
        }
        case 0x213D: {
            const u8 value = !vCounterHigh_ ? static_cast<u8>(vCounterLatch_) :
                static_cast<u8>((ppu2OpenBus_ & 0xFEu) | ((vCounterLatch_ >> 8) & 1u));
            vCounterHigh_ = !vCounterHigh_;
            ppu2OpenBus_ = value;
            return value;
        }
        case 0x213E: {
            const u8 value = static_cast<u8>((ppu1OpenBus_ & 0x10u) | 0x01u |
                (timeOver_ ? 0x80u : 0u) | (rangeOver_ ? 0x40u : 0u));
            ppu1OpenBus_ = value;
            return value;
        }
        case 0x213F: {
            // When WRIO bit 7 is zero, the external H/V latch is disabled
            // and STAT78 bit 6 is wired high. Reading STAT78 in this mode
            // must not clear a previously captured counter-latch flag.
            const bool statusLatch = !counterLatchEnabled_ || countersLatched_;
            const u8 value = static_cast<u8>((ppu2OpenBus_ & 0x20u) | 0x03u |
                (pal_ ? 0x10u : 0u) | (statusLatch ? 0x40u : 0u) |
                (field_ ? 0x80u : 0u));
            hCounterHigh_ = false;
            vCounterHigh_ = false;
            if (counterLatchEnabled_) countersLatched_ = false;
            ppu2OpenBus_ = value;
            return value;
        }
        default: return address <= 0x213Au ? ppu1OpenBus_ : ppu2OpenBus_;
    }
}

u16 PPU::rawColorFromCgram(unsigned index) const {
    index &= 0xFFu;
    const unsigned byte = index * 2u;
    return static_cast<u16>((cgram_[byte] | (static_cast<u16>(cgram_[byte + 1u]) << 8)) & 0x7FFFu);
}

u32 PPU::colorFromCgram(unsigned index) const { return colorFrom15Bit(rawColorFromCgram(index)); }

void PPU::rebuildColorLut() {
    // The original 5-bit -> 8-bit brightness formula is preserved exactly.
    // Fold 98,304 divisions per LUT rebuild into 512 one-time channel values.
    static const auto channel = [] {
        std::array<std::array<u8, 32>, 16> table{};
        for (unsigned brightness = 0; brightness < 16; ++brightness)
            for (unsigned component = 0; component < 32; ++component)
                table[brightness][component] = static_cast<u8>(
                    (component * brightness * 255u + 232u) / (31u * 15u));
        return table;
    }();
    const auto& level = channel[displayControl_ & 0x0Fu];
    for (unsigned raw = 0; raw < 32768u; ++raw) {
        const unsigned r = level[raw & 31u];
        const unsigned g = level[(raw >> 5) & 31u];
        const unsigned b = level[(raw >> 10) & 31u];
        colorLut_[raw] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

u32 PPU::colorFrom15Bit(u16 raw) const {
    return colorLut_[raw & 0x7FFFu];
}

u16 PPU::directColor(unsigned palette, unsigned pixel) const {
    const unsigned r = ((pixel & 0x07u) << 2) | ((palette & 0x01u) << 1);
    const unsigned g = (((pixel >> 3) & 0x07u) << 2) | (((palette >> 1) & 0x01u) << 1);
    const unsigned b = (((pixel >> 6) & 0x03u) << 3) | (((palette >> 2) & 0x01u) << 2);
    return static_cast<u16>((r & 0x1Fu) | ((g & 0x1Fu) << 5) | ((b & 0x1Fu) << 10));
}

u16 PPU::applyMath15(u16 mainColor, u16 subColor, bool subtract, bool halve) const {
    int mr = mainColor & 31;
    int mg = (mainColor >> 5) & 31;
    int mb = (mainColor >> 10) & 31;
    const int sr = subColor & 31;
    const int sg = (subColor >> 5) & 31;
    const int sb = (subColor >> 10) & 31;
    if (subtract) {
        mr = std::max(0, mr - sr);
        mg = std::max(0, mg - sg);
        mb = std::max(0, mb - sb);
    } else {
        mr = std::min(31, mr + sr);
        mg = std::min(31, mg + sg);
        mb = std::min(31, mb + sb);
    }
    if (halve) { mr >>= 1; mg >>= 1; mb >>= 1; }
    return static_cast<u16>(mr | (mg << 5) | (mb << 10));
}

bool PPU::windowContains(unsigned window, unsigned x) const {
    window &= 1u;
    const unsigned left = windowLeft_[window];
    const unsigned right = windowRight_[window];
    // SNES windows do not wrap around the edge of the screen.  A left edge
    // greater than the right edge describes an empty window.  Treating it as
    // a wraparound range can unexpectedly mask most of the screen and is a
    // common cause of large black regions during transitions.
    if (left > right) return false;
    return x >= left && x <= right;
}

bool PPU::windowMasksPixel(unsigned layer, unsigned x, bool useSubScreen) const {
    // Layers 0..3 are BG1..BG4, 4 is OBJ and 5 is the dedicated color
    // window. The color window shares WOBJSEL/WOBJLOG, but uses their upper
    // nibble/bits and is not gated by TMW/TSW. The previous implementation
    // accidentally evaluated color clipping with the OBJ window bits, which
    // can turn fades and title-screen transitions completely black.
    if (layer > 5u) return false;
    if (layer < 5u) {
        const u8 screenMask = useSubScreen ? subWindowMask_ : mainWindowMask_;
        if ((screenMask & (1u << layer)) == 0) return false;
    }

    unsigned selectRegister = 0;
    unsigned shift = 0;
    if (layer < 2u) { selectRegister = 0; shift = layer * 4u; }
    else if (layer < 4u) { selectRegister = 1; shift = (layer - 2u) * 4u; }
    else { selectRegister = 2; shift = layer == 5u ? 4u : 0u; }
    const u8 selection = static_cast<u8>((windowSelect_[selectRegister] >> shift) & 0x0Fu);
    const bool window1Enabled = (selection & 0x02u) != 0;
    const bool window2Enabled = (selection & 0x08u) != 0;
    bool in1 = windowContains(0, x);
    bool in2 = windowContains(1, x);
    if (selection & 0x01u) in1 = !in1;
    if (selection & 0x04u) in2 = !in2;
    if (!window1Enabled && !window2Enabled) return false;
    if (window1Enabled && !window2Enabled) return in1;
    if (!window1Enabled && window2Enabled) return in2;

    const unsigned logicRegister = layer < 4u ? 0u : 1u;
    const unsigned logicShift = layer < 4u ? layer * 2u : (layer == 5u ? 2u : 0u);
    const unsigned logic = (windowLogic_[logicRegister] >> logicShift) & 3u;
    if (logic == 0u) return in1 || in2;
    if (logic == 1u) return in1 && in2;
    if (logic == 2u) return in1 != in2;
    return in1 == in2;
}

bool PPU::windowRuleApplies(unsigned mode, bool inWindow) const {
    switch (mode & 3u) {
        case 0: return false;
        case 1: return !inWindow;
        case 2: return inWindow;
        default: return true;
    }
}

void PPU::invalidateTileCache(std::size_t byteAddress) {
    byteAddress &= 0xFFFFu;
    tileCache2Valid_[(byteAddress / 16u) & 4095u] = 0;
    tileCache4Valid_[(byteAddress / 32u) & 2047u] = 0;
    tileCache8Valid_[(byteAddress / 64u) & 1023u] = 0;
}

void PPU::decodeTile(unsigned bpp, std::size_t tileAddress) const {
    const unsigned bytesPerTile = bpp * 8u;
    const unsigned tileIndex = static_cast<unsigned>((tileAddress & 0xFFFFu) / bytesPerTile);
    std::vector<u8>* cache = 0;
    std::vector<u8>* valid = 0;
    if (bpp == 2u) { cache = &tileCache2_; valid = &tileCache2Valid_; }
    else if (bpp == 4u) { cache = &tileCache4_; valid = &tileCache4Valid_; }
    else { cache = &tileCache8_; valid = &tileCache8Valid_; }
    if ((*valid)[tileIndex]) return;
    const std::size_t base = static_cast<std::size_t>(tileIndex) * bytesPerTile;
    u8* output = &(*cache)[static_cast<std::size_t>(tileIndex) * 64u];
    for (unsigned y = 0; y < 8u; ++y) {
        const std::size_t row = (base + y * 2u) & 0xFFFFu;
        const u8 p0 = vram_[row];
        const u8 p1 = vram_[(row + 1u) & 0xFFFFu];
        const u8 p2 = bpp >= 4u ? vram_[(row + 16u) & 0xFFFFu] : 0;
        const u8 p3 = bpp >= 4u ? vram_[(row + 17u) & 0xFFFFu] : 0;
        const u8 p4 = bpp >= 8u ? vram_[(row + 32u) & 0xFFFFu] : 0;
        const u8 p5 = bpp >= 8u ? vram_[(row + 33u) & 0xFFFFu] : 0;
        const u8 p6 = bpp >= 8u ? vram_[(row + 48u) & 0xFFFFu] : 0;
        const u8 p7 = bpp >= 8u ? vram_[(row + 49u) & 0xFFFFu] : 0;
        for (unsigned x = 0; x < 8u; ++x) {
            const unsigned bit = 7u - x;
            output[y * 8u + x] = static_cast<u8>(
                (((p0 >> bit) & 1u) << 0) | (((p1 >> bit) & 1u) << 1) |
                (((p2 >> bit) & 1u) << 2) | (((p3 >> bit) & 1u) << 3) |
                (((p4 >> bit) & 1u) << 4) | (((p5 >> bit) & 1u) << 5) |
                (((p6 >> bit) & 1u) << 6) | (((p7 >> bit) & 1u) << 7));
        }
    }
    (*valid)[tileIndex] = 1;
}

unsigned PPU::tilePixel(unsigned bpp, std::size_t tileAddress, unsigned x, unsigned y) const {
    // Valid SNES tile formats are 2/4/8 bpp => 16/32/64 bytes per tile.
    // The old variable divisor blocked the compiler's shift specialization
    // in several hot inlining scenarios.
    const unsigned shift = bpp == 2u ? 4u : (bpp == 4u ? 5u : 6u);
    const unsigned tileIndex = static_cast<unsigned>((tileAddress & 0xFFFFu) >> shift);
    decodeTile(bpp, tileAddress);
    const std::size_t pixel = static_cast<std::size_t>(tileIndex) * 64u + (y & 7u) * 8u + (x & 7u);
    if (bpp == 2u) return tileCache2_[pixel];
    if (bpp == 4u) return tileCache4_[pixel];
    return tileCache8_[pixel];
}

u16 PPU::tilemapEntry(const Background& bg, unsigned tileX, unsigned tileY) const {
    const unsigned size = bg.screenBaseAndSize & 3u;
    const unsigned width = (size & 1u) ? 64u : 32u;
    const unsigned height = (size & 2u) ? 64u : 32u;
    // BG tilemap dimensions are powers of two (32 or 64).
    // Masking here avoids a dynamic integer divide for every fetched pixel.
    tileX &= width - 1u;
    tileY &= height - 1u;
    unsigned screen = 0;
    if (tileX >= 32u) screen += 1u;
    if (tileY >= 32u) screen += (width == 64u ? 2u : 1u);
    const unsigned localX = tileX & 31u;
    const unsigned localY = tileY & 31u;
    const std::size_t base = static_cast<std::size_t>(bg.screenBaseAndSize & 0xFCu) << 9;
    const std::size_t address = (base + screen * 0x800u + (localY * 32u + localX) * 2u) & 0xFFFFu;
    return static_cast<u16>(vram_[address] | (static_cast<u16>(vram_[(address + 1u) & 0xFFFFu]) << 8));
}

void PPU::putPixel(std::vector<Pixel>& target, unsigned x, unsigned,
                   u16 color, int priority, u8 layer,
                   bool colorMathEligible) {
    if (x >= static_cast<unsigned>(renderWidth_) || x >= target.size()) return;
    Pixel& destination = target[x];
    if (!destination.opaque || priority >= destination.priority) {
        destination.color = color;
        destination.priority = priority;
        destination.layer = layer;
        destination.opaque = true;
        destination.colorMathEligible = colorMathEligible;
        destination.backdrop = false;
    }
}

void PPU::drawBackground(unsigned bgIndex, unsigned bpp, bool mode0Palette,
                         bool useSubScreen, int lowPriority, int highPriority,
                         bool allowDirectColor, unsigned screenY,
                         unsigned sourceStart, unsigned sourceEnd) {
    if (bgIndex >= 4u) return;
    const u8 screenEnable = useSubScreen ? subScreen_ : mainScreen_;
    if ((screenEnable & (1u << bgIndex)) == 0) return;
    std::vector<Pixel>& target = useSubScreen ? subPixels_ : mainPixels_;
    const Background& bg = backgrounds_[bgIndex];
    const bool largeTiles = (bgMode_ & (1u << (4u + bgIndex))) != 0;
    const unsigned tileSize = largeTiles ? 16u : 8u;
    const unsigned bytesPerTile = bpp * 8u;
    const bool mosaicEnabled = (mosaic_ & (1u << bgIndex)) != 0;
    const unsigned mosaicSize = ((mosaic_ >> 4) & 0x0Fu) + 1u;
    const unsigned mode = bgMode_ & 7u;
    const unsigned sampledY = mosaicEnabled ? screenY - (screenY % mosaicSize) : screenY;

    sourceStart = std::min<unsigned>(sourceStart, static_cast<unsigned>(renderWidth_));
    sourceEnd = std::min<unsigned>(sourceEnd, static_cast<unsigned>(renderWidth_));
    // CP68: a scanline segment observes a single stable VRAM/tilemap state.
    // Reuse the decoded tile and tilemap word while adjacent output pixels
    // address the same source tile. Every raster-time VRAM/register write
    // flushes the active segment before mutating state, so cache lifetime is
    // intentionally confined to this invocation (never across writes).
    unsigned cachedMapX = ~0u, cachedMapY = ~0u;
    u16 cachedMapEntry = 0;
    std::size_t cachedTileAddress = ~std::size_t(0);
    const u8* cachedTilePixels = nullptr;
    const unsigned tileShift = bpp == 2u ? 4u : (bpp == 4u ? 5u : 6u);
    const std::vector<u8>& decodedTiles = bpp == 2u ? tileCache2_ : (bpp == 4u ? tileCache4_ : tileCache8_);
    const bool direct = allowDirectColor && (colorMathControl_ & 1u) != 0;
    // No per-pixel window test is needed unless the layer's mask is enabled
    // AND at least one of its two SNES windows is selected.
    const unsigned windowShift = (bgIndex & 1u) * 4u;
    const bool maskWindows = (((useSubScreen ? subWindowMask_ : mainWindowMask_) >> bgIndex) & 1u) != 0u &&
                             (windowSelect_[bgIndex / 2u] & (0x0Au << windowShift)) != 0u;
    for (unsigned screenX = sourceStart; screenX < sourceEnd; ++screenX) {
        const unsigned logicalX = (screenX * 256u) / static_cast<unsigned>(renderWidth_);
        if (maskWindows && windowMasksPixel(bgIndex, logicalX, useSubScreen)) continue;
        const unsigned sampledX = mosaicEnabled ? screenX - (screenX % mosaicSize) : screenX;
        unsigned worldX = (sampledX + bg.hOffset) & 0x03FFu;
        unsigned worldY = (sampledY + bg.vOffset) & 0x03FFu;

        if ((mode == 2u || mode == 4u || mode == 6u) && bgIndex < 2u && logicalX >= 8u) {
            const Background& offsetBg = backgrounds_[2];
            const u16 offset = tilemapEntry(offsetBg,
                ((logicalX - 8u + offsetBg.hOffset) >> 3),
                ((screenY + offsetBg.vOffset) >> 3));
            const bool applies = bgIndex == 0u ? (offset & 0x2000u) != 0 : (offset & 0x4000u) != 0;
            if (applies) {
                if (mode == 4u && (offset & 0x8000u)) worldY = (sampledY + (offset & 0x03FFu)) & 0x03FFu;
                else worldX = (sampledX + (offset & 0x03FFu)) & 0x03FFu;
            }
        }

        const unsigned mapTileX = worldX / tileSize;
        const unsigned mapTileY = worldY / tileSize;
        if (mapTileX != cachedMapX || mapTileY != cachedMapY) {
            cachedMapEntry = tilemapEntry(bg, mapTileX, mapTileY);
            cachedMapX = mapTileX;
            cachedMapY = mapTileY;
        }
        const u16 entry = cachedMapEntry;
        unsigned tileNumber = entry & 0x03FFu;
        unsigned px = worldX % tileSize;
        unsigned py = worldY % tileSize;
        if (entry & 0x4000u) px = tileSize - 1u - px;
        if (entry & 0x8000u) py = tileSize - 1u - py;
        if (largeTiles) {
            tileNumber += (px / 8u) + (py / 8u) * 16u;
            px &= 7u;
            py &= 7u;
        }
        const std::size_t tileAddress = (static_cast<std::size_t>(bg.tileBase) + tileNumber * bytesPerTile) & 0xFFFFu;
        if (tileAddress != cachedTileAddress) {
            decodeTile(bpp, tileAddress);
            cachedTileAddress = tileAddress;
            const std::size_t tileIndex = (tileAddress & 0xFFFFu) >> tileShift;
            cachedTilePixels = decodedTiles.data() + tileIndex * 64u;
        }
        const unsigned pixel = cachedTilePixels[(py & 7u) * 8u + (px & 7u)];
        if (pixel == 0) continue;
        const unsigned palette = (entry >> 10) & 7u;
        u16 color;
        if (direct) color = directColor(palette, pixel);
        else {
            unsigned colorIndex;
            if (mode0Palette) colorIndex = bgIndex * 32u + palette * 4u + pixel;
            else colorIndex = palette * (1u << bpp) + pixel;
            color = rawColorFromCgram(colorIndex);
        }
        putPixel(target, screenX, 0, color,
                 (entry & 0x2000u) ? highPriority : lowPriority,
                 static_cast<u8>(bgIndex));
    }
}

void PPU::drawMode7(bool useSubScreen, unsigned screenY,
                    unsigned sourceStart, unsigned sourceEnd) {
    const u8 screenEnable = useSubScreen ? subScreen_ : mainScreen_;
    if ((screenEnable & 1u) == 0 && !((setIni_ & 0x40u) && (screenEnable & 2u))) return;
    std::vector<Pixel>& target = useSubScreen ? subPixels_ : mainPixels_;
    const bool flipX = (mode7Control_ & 1u) != 0;
    const bool flipY = (mode7Control_ & 2u) != 0;
    const unsigned outsideMode = (mode7Control_ >> 6) & 3u;
    const bool extBg = (setIni_ & 0x40u) != 0;
    const int centerX = static_cast<int>(mode7X_);
    const int centerY = static_cast<int>(mode7Y_);
    const int hOffset = static_cast<int>(mode7HOffset_);
    const int vOffset = static_cast<int>(mode7VOffset_);
    const int y = flipY ? visibleLineCount() - 1 - static_cast<int>(screenY) : static_cast<int>(screenY);

    sourceStart = std::min<unsigned>(sourceStart, static_cast<unsigned>(renderWidth_));
    sourceEnd = std::min<unsigned>(sourceEnd, static_cast<unsigned>(renderWidth_));
    for (int screenX = static_cast<int>(sourceStart); screenX < static_cast<int>(sourceEnd); ++screenX) {
        const unsigned logicalX = static_cast<unsigned>(screenX * 256 / renderWidth_);
        const int x = flipX ? renderWidth_ - 1 - screenX : screenX;
        const int relativeX = x + hOffset - centerX;
        const int relativeY = y + vOffset - centerY;
        int transformedX = ((static_cast<int>(mode7A_) * relativeX +
                             static_cast<int>(mode7B_) * relativeY) >> 8) + centerX;
        int transformedY = ((static_cast<int>(mode7C_) * relativeX +
                             static_cast<int>(mode7D_) * relativeY) >> 8) + centerY;
        const bool outside = transformedX < 0 || transformedY < 0 || transformedX >= 1024 || transformedY >= 1024;
        if (outside) {
            if (outsideMode == 0u || outsideMode == 1u) {
                transformedX &= 1023;
                transformedY &= 1023;
            } else if (outsideMode == 2u) continue;
            else { transformedX &= 7; transformedY &= 7; }
        }
        const unsigned tileX = static_cast<unsigned>(transformedX >> 3) & 127u;
        const unsigned tileY = static_cast<unsigned>(transformedY >> 3) & 127u;
        const std::size_t mapWord = static_cast<std::size_t>(tileY * 128u + tileX) & 0x3FFFu;
        const unsigned tile = vram_[(mapWord * 2u) & 0xFFFFu];
        const unsigned pixelX = static_cast<unsigned>(transformedX) & 7u;
        const unsigned pixelY = static_cast<unsigned>(transformedY) & 7u;
        const std::size_t characterWord = (static_cast<std::size_t>(tile) * 64u + pixelY * 8u + pixelX) & 0x7FFFu;
        const unsigned colorByte = vram_[(characterWord * 2u + 1u) & 0xFFFFu];
        if (colorByte == 0) continue;

        const bool highExt = extBg && (colorByte & 0x80u) != 0;
        const unsigned layer = highExt ? 1u : 0u;
        if ((screenEnable & (1u << layer)) == 0) continue;
        if (windowMasksPixel(layer, logicalX, useSubScreen)) continue;
        const unsigned paletteIndex = extBg ? (colorByte & 0x7Fu) : colorByte;
        // EXTBG's second Mode-7 layer always uses CGRAM.  Direct colour only
        // applies to the normal Mode-7 BG1 result.
        const bool direct = !highExt && (colorMathControl_ & 1u) != 0;
        const u16 color = direct ? directColor(0, colorByte)
                                 : rawColorFromCgram(paletteIndex);
        putPixel(target, static_cast<unsigned>(screenX), 0,
                 color, highExt ? 10 : 4, static_cast<u8>(layer));
    }
}

unsigned PPU::spriteWidth(bool large) const {
    static const unsigned widths[8][2] = {
        {8,16}, {8,32}, {8,64}, {16,32},
        {16,64}, {32,64}, {16,32}, {16,32}
    };
    return widths[(objectSelection_ >> 5) & 7u][large ? 1 : 0];
}

unsigned PPU::spriteHeight(bool large) const {
    static const unsigned heights[8][2] = {
        {8,16}, {8,32}, {8,64}, {16,32},
        {16,64}, {32,64}, {32,64}, {32,32}
    };
    return heights[(objectSelection_ >> 5) & 7u][large ? 1 : 0];
}

void PPU::updateSpriteOverflowLine(unsigned screenY) {
    // The 32-object range limit is applied by forward OAM evaluation.  The
    // 34-tile fetch budget, by contrast, is spent *backwards* through those
    // accepted objects, and only visible tiles spend a slot.  In particular,
    // a partially fetched object must not be discarded as a whole.
    unsigned selected[32];
    unsigned count = 0;
    for (unsigned evaluated = 0; evaluated < 128u; ++evaluated) {
        const unsigned sprite = (static_cast<unsigned>(oamFirstSprite_) + evaluated) & 127u;
        const unsigned low = sprite * 4u;
        const unsigned packed = oam_[512u + sprite / 4u];
        const unsigned shift = (sprite & 3u) * 2u;
        const unsigned y = oam_[low + 1u];
        const bool large = ((packed >> (shift + 1u)) & 1u) != 0;
        if (((screenY - y) & 0xFFu) >= spriteHeight(large)) continue;
        const unsigned x = static_cast<unsigned>(oam_[low]) | (((packed >> shift) & 1u) << 8);
        const unsigned width = spriteWidth(large);
        if (x > 256u && x + width - 1u < 512u) continue;
        if (count >= 32u) { rangeOver_ = true; break; }
        selected[count++] = sprite;
    }
    unsigned tiles = 0;
    for (unsigned i = count; i > 0; --i) {
        const unsigned sprite = selected[i - 1u];
        const unsigned packed = oam_[512u + sprite / 4u];
        const unsigned shift = (sprite & 3u) * 2u;
        const unsigned x = static_cast<unsigned>(oam_[sprite * 4u]) | (((packed >> shift) & 1u) << 8);
        const unsigned width = spriteWidth(((packed >> (shift + 1u)) & 1u) != 0);
        for (unsigned tile = 0; tile < width / 8u; ++tile) {
            const unsigned sx = (x + tile * 8u) & 511u;
            if (x != 256u && sx >= 256u && sx + 7u < 512u) continue;
            if (++tiles > 34u) { timeOver_ = true; return; }
        }
    }
}

void PPU::drawSprites(bool useSubScreen, unsigned screenY,
                      unsigned sourceStart, unsigned sourceEnd) {
    const u8 screenEnable = useSubScreen ? subScreen_ : mainScreen_;
    if ((screenEnable & 0x10u) == 0) return;
    std::vector<Pixel>& target = useSubScreen ? subPixels_ : mainPixels_;
    const std::size_t nameBase = (static_cast<std::size_t>(objectSelection_ & 7u) * 0x4000u) & 0xFFFFu;
    const std::size_t nameSelect = (static_cast<std::size_t>(((objectSelection_ >> 3) & 3u) + 1u) * 0x2000u);
    int priorities[4] = { 2, 5, 8, 11 };
    if ((bgMode_ & 7u) == 1u) {
        if (bgMode_ & 0x08u) {
            priorities[0] = 2; priorities[1] = 3;
            priorities[2] = 6; priorities[3] = 9;
        } else {
            priorities[0] = 2; priorities[1] = 4;
            priorities[2] = 7; priorities[3] = 10;
        }
    }

    // Evaluate up to 32 on-scanline objects in OAM priority order, including
    // the rotating first-sprite start. Entirely offscreen objects do not
    // consume slots, except the hardware's special X=256 case.
    unsigned selectedSprites[32];
    unsigned selectedCount = 0;
    for (unsigned evaluated = 0; evaluated < 128u; ++evaluated) {
        const unsigned sprite = (static_cast<unsigned>(oamFirstSprite_) + evaluated) & 127u;
        const unsigned low = sprite * 4u;
        const unsigned packed = oam_[512u + sprite / 4u];
        const unsigned shift = (sprite & 3u) * 2u;
        const bool large = ((packed >> (shift + 1u)) & 1u) != 0;
        const unsigned sy = (screenY - oam_[low + 1u]) & 0xFFu;
        if (sy >= spriteHeight(large)) continue;
        const unsigned x = static_cast<unsigned>(oam_[low]) | (((packed >> shift) & 1u) << 8);
        if (x > 256u && x + spriteWidth(large) - 1u < 512u) continue;
        if (selectedCount == 32u) break;
        selectedSprites[selectedCount++] = sprite;
    }

    // Fetch the OBJ tile row in reverse evaluated-object order. The first
    // 34 *eligible* tiles win fetch bandwidth; an object may be partially
    // fetched. Draw in that same order so earlier OAM objects win equal-
    // priority overlaps, matching the PPU tile pipeline.
    const bool maskObjWindow = (((useSubScreen ? subWindowMask_ : mainWindowMask_) & 0x10u) != 0u) &&
                               ((windowSelect_[2] & 0x0Au) != 0u);
    unsigned fetchedTiles = 0;
    for (unsigned i = selectedCount; i > 0; --i) {
        bool firstEligibleTile = true;
        bool skipEntireObject = false;
        const unsigned sprite = selectedSprites[i - 1u];
        const unsigned low = sprite * 4u;
        const unsigned packed = oam_[512u + sprite / 4u];
        const unsigned shift = (sprite & 3u) * 2u;
        const unsigned rawX = static_cast<unsigned>(oam_[low]) | (((packed >> shift) & 1u) << 8);
        const unsigned y = oam_[low + 1u];
        const u8 attr = oam_[low + 3u];
        const unsigned tileNumber = oam_[low + 2u];
        const bool secondNameTable = (attr & 1u) != 0;
        const unsigned palette = (attr >> 1) & 7u;
        const unsigned priority = (attr >> 4) & 3u;
        const bool hflip = (attr & 0x40u) != 0;
        const bool vflip = (attr & 0x80u) != 0;
        const bool large = ((packed >> (shift + 1u)) & 1u) != 0;
        const unsigned width = spriteWidth(large);
        const unsigned height = spriteHeight(large);
        const unsigned sy = (screenY - y) & 0xFFu;
        // On rectangular OBJ, V flip operates within width-high halves.
        unsigned sourceY = sy;
        if (vflip) {
            if (width == height) sourceY = height - 1u - sy;
            else if (sy < width) sourceY = width - 1u - sy;
            else sourceY = width + (width - 1u) - (sy - width);
        }
        for (unsigned tx = 0; tx < width / 8u; ++tx) {
            const unsigned tileX = (rawX + tx * 8u) & 511u;
            if (rawX != 256u && tileX >= 256u && tileX + 7u < 512u) continue;
            if (fetchedTiles >= 34u) return;
            const bool blocked = (objFetchBlockedCurrent_ & (1ull << fetchedTiles)) != 0ull;
            ++fetchedTiles;
            // In particular, blocking the FIRST fetch of an object can
            // suppress the entire object, even if blank is released later.
            if (firstEligibleTile) { skipEntireObject = blocked; firstEligibleTile = false; }
            if (skipEntireObject || blocked) continue;
            const unsigned sourceTileX = hflip ? width / 8u - 1u - tx : tx;
            // OBJ character names wrap in their 16-column table. A carry
            // from the low nibble must NOT change the tile row.
            const unsigned subTile = ((tileNumber + sourceTileX) & 15u) |
                ((tileNumber + (sourceY / 8u) * 16u) & 0xF0u);
            const std::size_t tileAddress = (nameBase + (secondNameTable ? nameSelect : 0u) +
                                             subTile * 32u) & 0xFFFFu;
            for (unsigned px = 0; px < 8u; ++px) {
                const unsigned screenX = (tileX + px) & 511u;
                if (screenX >= 256u) continue;
                if (maskObjWindow && windowMasksPixel(4u, screenX, useSubScreen)) continue;
                const unsigned tilePixelX = hflip ? 7u - px : px;
                const unsigned pixel = tilePixel(4u, tileAddress, tilePixelX, sourceY & 7u);
                if (pixel == 0u) continue;
                const u16 color = rawColorFromCgram(128u + palette * 16u + pixel);
                if (renderWidth_ == 512) {
                    const unsigned first = screenX * 2u;
                    if (first >= sourceStart && first < sourceEnd)
                        putPixel(target, first, 0, color, priorities[priority], 4u, palette >= 4u);
                    if (first + 1u >= sourceStart && first + 1u < sourceEnd)
                        putPixel(target, first + 1u, 0, color, priorities[priority], 4u, palette >= 4u);
                } else if (screenX >= sourceStart && screenX < sourceEnd) {
                    putPixel(target, screenX, 0, color, priorities[priority], 4u, palette >= 4u);
                }
            }
        }
    }
}

void PPU::ensureFrameGeometry(int desiredWidth, int desiredHeight) {
    desiredWidth = desiredWidth >= HiresWidth ? HiresWidth : FrameWidth;
    desiredHeight = std::max(static_cast<int>(FrameHeight), std::min(static_cast<int>(InterlaceOverscanHeight), desiredHeight));
    if (!frameSurfacePrepared_) {
        outputWidth_ = desiredWidth;
        outputHeight_ = desiredHeight;
        frameBuffer_.assign(static_cast<std::size_t>(outputWidth_) * outputHeight_, 0xFF000000u);
        layerBuffer_.assign(static_cast<std::size_t>(outputWidth_) * outputHeight_, 5u);
        frameSurfacePrepared_ = true;
        return;
    }
    if (desiredWidth <= outputWidth_ && desiredHeight <= outputHeight_) return;

    const int oldWidth = outputWidth_;
    const int oldHeight = outputHeight_;
    const int newWidth = std::max(oldWidth, desiredWidth);
    const int newHeight = std::max(oldHeight, desiredHeight);
    std::vector<u32> newFrame(static_cast<std::size_t>(newWidth) * newHeight, 0xFF000000u);
    std::vector<u8> newLayer(static_cast<std::size_t>(newWidth) * newHeight, 5u);
    for (int y = 0; y < oldHeight; ++y) {
        if (oldWidth == newWidth) {
            std::copy(frameBuffer_.begin() + static_cast<std::size_t>(y) * oldWidth,
                      frameBuffer_.begin() + static_cast<std::size_t>(y + 1) * oldWidth,
                      newFrame.begin() + static_cast<std::size_t>(y) * newWidth);
            std::copy(layerBuffer_.begin() + static_cast<std::size_t>(y) * oldWidth,
                      layerBuffer_.begin() + static_cast<std::size_t>(y + 1) * oldWidth,
                      newLayer.begin() + static_cast<std::size_t>(y) * newWidth);
        } else {
            for (int x = 0; x < oldWidth; ++x) {
                const std::size_t src = static_cast<std::size_t>(y) * oldWidth + x;
                const std::size_t dst = static_cast<std::size_t>(y) * newWidth + x * 2;
                newFrame[dst] = newFrame[dst + 1u] = frameBuffer_[src];
                newLayer[dst] = newLayer[dst + 1u] = layerBuffer_[src];
            }
        }
    }
    frameBuffer_.swap(newFrame);
    layerBuffer_.swap(newLayer);
    outputWidth_ = newWidth;
    outputHeight_ = newHeight;
}

void PPU::beginFrameSurface() {
    const unsigned mode = bgMode_ & 7u;
    const bool hires = mode == 5u || mode == 6u || (setIni_ & 0x08u) != 0;
    const bool interlace = (setIni_ & 0x01u) != 0;
    const int height = visibleLineCount() * (interlace ? 2 : 1);
    ensureFrameGeometry(hires ? HiresWidth : FrameWidth, height);
}

void PPU::composeScanline(unsigned screenY, unsigned logicalStart, unsigned logicalEnd) {
    const bool pseudoHires = (setIni_ & 0x08u) != 0 && renderWidth_ == FrameWidth;
    const bool interlace = (setIni_ & 0x01u) != 0;
    const bool forcedBlankActive = (displayControl_ & 0x80u) != 0;
    const int desiredWidth = (pseudoHires || renderWidth_ == HiresWidth) ? HiresWidth : FrameWidth;
    const int desiredHeight = visibleLineCount() * (interlace ? 2 : 1);
    ensureFrameGeometry(desiredWidth, desiredHeight);
    if (desiredWidth == HiresWidth) frameHadHires_ = true;

    const bool subtract = (colorMathEnable_ & 0x80u) != 0;
    const bool halve = (colorMathEnable_ & 0x40u) != 0;
    const bool useSubScreenForMath = (colorMathControl_ & 0x02u) != 0;
    const unsigned clipMode = (colorMathControl_ >> 6) & 3u;
    const unsigned preventMode = (colorMathControl_ >> 4) & 3u;
    // If neither color clipping nor window-conditioned math is selected,
    // querying the dedicated color window for every output pixel is needless.
    const bool needsColorWindow = clipMode != 0u ||
        (preventMode != 0u && (colorMathEnable_ & 0x3Fu) != 0u);

    logicalStart = std::min<unsigned>(FrameWidth, logicalStart);
    logicalEnd = std::min<unsigned>(FrameWidth, std::max(logicalStart, logicalEnd));
    const int sourceStart = static_cast<int>((static_cast<unsigned long long>(logicalStart) *
                                              static_cast<unsigned>(renderWidth_)) / FrameWidth);
    const int sourceEnd = static_cast<int>((static_cast<unsigned long long>(logicalEnd) *
                                            static_cast<unsigned>(renderWidth_)) / FrameWidth);

    const unsigned rowCount = interlace ? 2u : 1u;
    const unsigned firstRow = interlace ? screenY * 2u : screenY;
    for (int x = sourceStart; x < sourceEnd; ++x) {
        const Pixel& main = mainPixels_[static_cast<std::size_t>(x)];
        const Pixel& sub = subPixels_[static_cast<std::size_t>(x)];
        const unsigned logicalX = static_cast<unsigned>(x * 256 / renderWidth_);
        const bool inColorWindow = needsColorWindow && windowMasksPixel(5u, logicalX, false);
        u16 mainColor = main.color;
        const bool mainClippedToBlack = windowRuleApplies(clipMode, inColorWindow);
        if (mainClippedToBlack) mainColor = 0;
        const unsigned enableBit = main.layer <= 4u ? main.layer : 5u;
        // OBJ color math is restricted by hardware to palettes 4-7.
        // Palettes 0-3 must remain opaque even when CGADSUB enables OBJ;
        // treating every OBJ palette as math-capable corrupts ordinary
        // sprites over Mode 7/subscreen backgrounds (notably SMW's map).
        const bool mathEnabled = main.colorMathEligible &&
                                 (colorMathEnable_ & (1u << enableBit)) != 0 &&
                                 !windowRuleApplies(preventMode, inColorWindow);
        if (mathEnabled) {
            const u16 second = useSubScreenForMath ? sub.color : fixedColor_;
            // Hardware suppresses the divide-by-two operation when the main
            // screen was forcibly clipped to black.  It also suppresses it
            // when colour math selects the sub screen and that sub result is
            // merely the fixed-colour backdrop (no BG/OBJ pixel survived).
            const bool effectiveHalve = halve && !mainClippedToBlack &&
                !(useSubScreenForMath && sub.backdrop);
            mainColor = applyMath15(mainColor, second, subtract, effectiveHalve);
        }

        if (forcedBlankActive) mainColor = 0;

        for (unsigned rowOffset = 0; rowOffset < rowCount; ++rowOffset) {
            const std::size_t rowBase = static_cast<std::size_t>(firstRow + rowOffset) * outputWidth_;
            if (pseudoHires) {
                const std::size_t dst = rowBase + static_cast<std::size_t>(x) * 2u;
                frameBuffer_[dst] = colorFrom15Bit(forcedBlankActive ? 0u : sub.color);
                layerBuffer_[dst] = sub.layer;
                frameBuffer_[dst + 1u] = colorFrom15Bit(mainColor);
                layerBuffer_[dst + 1u] = main.layer;
            } else if (renderWidth_ == HiresWidth) {
                const std::size_t dst = rowBase + static_cast<std::size_t>(x);
                frameBuffer_[dst] = colorFrom15Bit(mainColor);
                layerBuffer_[dst] = main.layer;
            } else if (outputWidth_ == HiresWidth) {
                const std::size_t dst = rowBase + static_cast<std::size_t>(x) * 2u;
                const u32 color = colorFrom15Bit(mainColor);
                frameBuffer_[dst] = frameBuffer_[dst + 1u] = color;
                layerBuffer_[dst] = layerBuffer_[dst + 1u] = main.layer;
            } else {
                const std::size_t dst = rowBase + static_cast<std::size_t>(x);
                frameBuffer_[dst] = colorFrom15Bit(mainColor);
                layerBuffer_[dst] = main.layer;
            }
        }
    }
}

void PPU::renderScanline(unsigned visibleY) {
    renderScanlineSegment(visibleY, 0u, FrameWidth);
}

void PPU::flushActiveDisplayToCurrentPosition() {
    if (!renderFrameEnabled_ || scanline_ < 1 || scanline_ > visibleLineCount()) return;
    // Physical output occupies H=22..277.  Register writes before H=22 affect
    // the whole line; writes after H=277 affect the next line.  Long dots are
    // already reflected by dot_, which is derived from master-clock position.
    int end = dot_ - 22;
    if (end < 0) end = 0;
    if (end > FrameWidth) end = FrameWidth;
    const unsigned logicalEnd = static_cast<unsigned>(end);
    if (logicalEnd <= renderedPixelsThisLine_) return;
    renderScanlineSegment(static_cast<unsigned>(scanline_ - 1),
                          renderedPixelsThisLine_, logicalEnd);
    renderedPixelsThisLine_ = logicalEnd;
}

void PPU::renderScanlineSegment(unsigned visibleY, unsigned logicalStart, unsigned logicalEnd) {
    if (visibleY >= static_cast<unsigned>(visibleLineCount()) || logicalStart >= logicalEnd) return;
    const unsigned mode = bgMode_ & 7u;
    renderWidth_ = (mode == 5u || mode == 6u) ? HiresWidth : FrameWidth;
    beginFrameSurface();

    logicalStart = std::min<unsigned>(FrameWidth, logicalStart);
    logicalEnd = std::min<unsigned>(FrameWidth, std::max(logicalStart, logicalEnd));
    const unsigned sourceStart = static_cast<unsigned>((static_cast<unsigned long long>(logicalStart) *
        static_cast<unsigned>(renderWidth_)) / FrameWidth);
    const unsigned sourceEnd = static_cast<unsigned>((static_cast<unsigned long long>(logicalEnd) *
        static_cast<unsigned>(renderWidth_)) / FrameWidth);
    if (sourceStart >= sourceEnd) return;

    // Raster writes can split a line into many segments.  Keep persistent
    // per-line buffers and clear only the pixels that are about to be drawn;
    // the old renderer rebuilt all 256/512 pixels after every register write.
    if (mainPixels_.size() != static_cast<std::size_t>(renderWidth_))
        mainPixels_.resize(static_cast<std::size_t>(renderWidth_));
    if (subPixels_.size() != static_cast<std::size_t>(renderWidth_))
        subPixels_.resize(static_cast<std::size_t>(renderWidth_));

    // The main-screen backdrop is CGRAM colour 0.  In ordinary low-resolution
    // modes the sub-screen backdrop is the fixed colour written through
    // COLDATA ($2132).  Initialising both screens from CGRAM[0] loses exactly
    // the fixed-colour backdrop that games such as Super Mario World blend
    // into their title-screen sky, producing the classic black-sky symptom.
    // True/pseudo-hires is the documented exception: both backdrop samples
    // come from CGRAM colour 0 because the two screens are interleaved.
    const u16 mainBackdrop = rawColorFromCgram(0);
    const bool hiresBackdrop = renderWidth_ == HiresWidth || (setIni_ & 0x08u) != 0;
    const u16 subBackdrop = hiresBackdrop ? mainBackdrop : fixedColor_;

    Pixel mainBackdropPixel;
    mainBackdropPixel.color = mainBackdrop;
    mainBackdropPixel.priority = 0;
    mainBackdropPixel.layer = 5;
    mainBackdropPixel.opaque = true;
    mainBackdropPixel.backdrop = true;
    Pixel subBackdropPixel = mainBackdropPixel;
    subBackdropPixel.color = subBackdrop;
    std::fill(mainPixels_.begin() + sourceStart, mainPixels_.begin() + sourceEnd, mainBackdropPixel);
    std::fill(subPixels_.begin() + sourceStart, subPixels_.begin() + sourceEnd, subBackdropPixel);

    if ((displayControl_ & 0x80u) == 0) {
        for (unsigned screen = 0; screen < 2u; ++screen) {
            const bool sub = screen != 0;
            switch (mode) {
                case 0:
                    drawBackground(3, 2, true, sub, 1, 5, false, visibleY, sourceStart, sourceEnd);
                    drawBackground(2, 2, true, sub, 2, 6, false, visibleY, sourceStart, sourceEnd);
                    drawBackground(1, 2, true, sub, 3, 7, false, visibleY, sourceStart, sourceEnd);
                    drawBackground(0, 2, true, sub, 4, 8, false, visibleY, sourceStart, sourceEnd);
                    break;
                case 1: {
                    // Mode 1 has a non-linear priority ladder. When bit 3 is
                    // clear: OBJ3 > BG1H > BG2H > OBJ2 > BG1L > BG2L >
                    // OBJ1 > BG3H > OBJ0 > BG3L. When set, BG3H moves above
                    // OBJ3 and the remaining levels close up beneath it.
                    const bool bg3Top = (bgMode_ & 0x08u) != 0;
                    drawBackground(2, 2, false, sub, 1, bg3Top ? 10 : 3, false,
                                   visibleY, sourceStart, sourceEnd);
                    drawBackground(1, 4, false, sub, bg3Top ? 4 : 5,
                                   bg3Top ? 7 : 8, false, visibleY, sourceStart, sourceEnd);
                    drawBackground(0, 4, false, sub, bg3Top ? 5 : 6,
                                   bg3Top ? 8 : 9, false, visibleY, sourceStart, sourceEnd);
                    break;
                }
                case 2:
                    drawBackground(1, 4, false, sub, 3, 7, false, visibleY, sourceStart, sourceEnd);
                    drawBackground(0, 4, false, sub, 4, 8, false, visibleY, sourceStart, sourceEnd);
                    break;
                case 3:
                    drawBackground(1, 4, false, sub, 3, 7, false, visibleY, sourceStart, sourceEnd);
                    drawBackground(0, 8, false, sub, 4, 8, true, visibleY, sourceStart, sourceEnd);
                    break;
                case 4:
                    drawBackground(1, 2, false, sub, 3, 7, false, visibleY, sourceStart, sourceEnd);
                    drawBackground(0, 8, false, sub, 4, 8, true, visibleY, sourceStart, sourceEnd);
                    break;
                case 5:
                    drawBackground(1, 2, false, sub, 3, 7, false, visibleY, sourceStart, sourceEnd);
                    drawBackground(0, 4, false, sub, 4, 8, false, visibleY, sourceStart, sourceEnd);
                    break;
                case 6:
                    drawBackground(0, 4, false, sub, 4, 8, false, visibleY, sourceStart, sourceEnd);
                    break;
                case 7:
                    drawMode7(sub, visibleY, sourceStart, sourceEnd);
                    break;
                default: break;
            }
            drawSprites(sub, visibleY, sourceStart, sourceEnd);
        }
    } else {
        Pixel blank;
        blank.color = 0;
        blank.priority = 32767;
        blank.layer = 5;
        blank.opaque = true;
        blank.backdrop = true;
        std::fill(mainPixels_.begin() + sourceStart, mainPixels_.begin() + sourceEnd, blank);
        std::fill(subPixels_.begin() + sourceStart, subPixels_.begin() + sourceEnd, blank);
    }

    // Sprite overflow is line-global, but computing it more than once after a
    // raster split is unnecessary and expensive.
    if (logicalEnd == FrameWidth) updateSpriteOverflowLine(visibleY);
    composeScanline(visibleY, logicalStart, logicalEnd);
}

const std::vector<u32>& PPU::frameBuffer() const { return frameBuffer_; }
const std::vector<u8>& PPU::vram() const { return vram_; }
const std::vector<u8>& PPU::cgram() const { return cgram_; }
const std::vector<u8>& PPU::oam() const { return oam_; }
int PPU::outputWidth() const { return outputWidth_; }
int PPU::outputHeight() const { return outputHeight_; }
int PPU::scanline() const { return scanline_; }
int PPU::dot() const { return dot_; }
int PPU::masterClock() const { return masterClock_; }
int PPU::lineMasterClocks() const { return lineMasterClocks_; }
int PPU::totalScanlines() const { return totalScanlines_; }
double PPU::frameRate() const { return pal_ ? 50.0069789 : 60.0988139; }
bool PPU::isPal() const { return pal_; }
u64 PPU::frameNumber() const { return frameNumber_; }
bool PPU::inVBlank() const { return vblank_; }
bool PPU::inHBlank() const { return masterClock_ < 4 || masterClock_ >= 1096; }
bool PPU::field() const { return field_; }
bool PPU::consumeHBlankStart() { const bool value = hblankStartEvent_; hblankStartEvent_ = false; return value; }
bool PPU::consumeHdmaStart() { const bool value = hdmaStartEvent_; hdmaStartEvent_ = false; return value; }
bool PPU::consumeVBlankStart() { const bool value = vblankStartEvent_; vblankStartEvent_ = false; return value; }
bool PPU::consumeFrameStart() { const bool value = frameStartEvent_; frameStartEvent_ = false; return value; }
bool PPU::consumeRefreshStart() { const bool value = refreshStartEvent_; refreshStartEvent_ = false; return value; }
bool PPU::forcedBlank() const { return (displayControl_ & 0x80u) != 0; }
u8 PPU::brightness() const { return displayControl_ & 0x0Fu; }
u8 PPU::mainScreenMask() const { return mainScreen_; }
u8 PPU::subScreenMask() const { return subScreen_; }
u8 PPU::colorMathControl() const { return colorMathControl_; }
u8 PPU::colorMathEnable() const { return colorMathEnable_; }
u8 PPU::setIni() const { return setIni_; }
u16 PPU::backdropColor() const { return rawColorFromCgram(0); }
u16 PPU::fixedColor() const { return fixedColor_; }
u8 PPU::backgroundMode() const { return bgMode_ & 7u; }
u16 PPU::backgroundHorizontalOffset(unsigned index) const { return index < 4u ? backgrounds_[index].hOffset : 0u; }
u16 PPU::backgroundVerticalOffset(unsigned index) const { return index < 4u ? backgrounds_[index].vOffset : 0u; }
u64 PPU::blockedVramAccesses() const { return blockedVramAccesses_; }
u64 PPU::blockedCgramAccesses() const { return blockedCgramAccesses_; }
u64 PPU::blockedOamAccesses() const { return blockedOamAccesses_; }
void PPU::setFrameRenderingEnabled(bool enabled) { renderFrameEnabled_ = enabled; }
bool PPU::consumeFrameReady() { const bool ready = frameReady_; frameReady_ = false; return ready; }

} // namespace snes
