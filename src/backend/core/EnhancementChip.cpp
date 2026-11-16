#include "snes/core/EnhancementChip.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>

namespace snes {

namespace {
static const double Pi = 3.1415926535897932384626433832795;
static std::size_t mirror(std::size_t address, std::size_t size) {
    if (size == 0u) return 0u;
    if ((size & (size - 1u)) == 0u) return address & (size - 1u);
    return address % size;
}
// The BWPA field selects 256 << BWPA protected bytes, saturated to the
// 256-KiB physical BW-RAM window (values above 0x0A cannot protect more).
static std::size_t sa1ProtectedBytes(u8 bwpa) {
    const unsigned code = bwpa & 15u;
    return static_cast<std::size_t>(256u) << std::min(code, 10u);
}
static bool sa1BwramWriteAllowed(u8 swen, u8 cwen, u8 bwpa, std::size_t address) {
    // Enabling either processor's BW-RAM write gate enables shared writes.
    // With both gates clear, BWPA protects the low memory interval only.
    return ((swen | cwen) & 0x80u) != 0u || address >= sa1ProtectedBytes(bwpa);
}
// BWPA compares the full SA-1 bus address *before* physical BW-RAM
// mirroring. A 128 KiB cartridge can therefore have different protection
// results at $40:0000 and $42:0000 even though both address the same cell.
static std::size_t sa1BwramProtectionAddress(u32 address, bool sa1Side,
                                             u8 sbmap, u8 cbmap, u8 bitmapMode) {
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 off = static_cast<u16>(address);
    if (bank >= 0x40u && bank <= (sa1Side ? 0x5Fu : 0x4Fu))
        return ((static_cast<std::size_t>(bank - 0x40u) << 16) | off) & 0x3FFFFu;
    if (sa1Side && bank >= 0x60u && bank <= 0x6Fu) {
        const std::size_t pixel = (static_cast<std::size_t>(bank - 0x60u) << 16) | off;
        return pixel >> ((bitmapMode & 0x80u) ? 2u : 1u);
    }
    const unsigned page = (sa1Side ? cbmap & 0x7Fu : sbmap & 0x1Fu);
    return (static_cast<std::size_t>(page) << 13) | (off & 0x1FFFu);
}
static u32 make24(const std::vector<u8>& r, unsigned o) {
    return static_cast<u32>(r[o]) | (static_cast<u32>(r[o + 1u]) << 8) |
           (static_cast<u32>(r[o + 2u]) << 16);
}
}

EnhancementChip::EnhancementChip()
    : kind_(None), mapMode_(0), cartridgeType_(0), title_(), romSize_(0), operations_(0),
      dspVariant_(1), dspCommand_(0), dspExpected_(0), dspStage_(0), dspTransparent_(0),
      dspLengthA_(0), dspLengthB_(0), dspInput_(), dspOutput_(),
      cx4Ram_(0x2000u, 0), sa1Iram_(0x800u, 0), sa1Bwram_(0x40000u, 0),
      sa1Regs_(0x200u, 0), sa1ArithmeticResult_(0), sa1Overflow_(false),
      sa1Cpu_(), sa1ActiveRom_(0), sa1ActiveBwram_(0), sa1ActiveRamDirty_(0),
      sa1ClockRemainder_(0), sa1Running_(false), sa1TimerClocks_(0),
      sa1VariableAddress_(0), sa1VariableBitOffset_(0), sa1LatchedH_(0),
      sa1LatchedV_(0), sa1Cc2Line_(0), sa1Cc2Tile_(0),
      superFxR_(), superFxStatus_(0), superFxPbr_(0), superFxRombr_(0),
      superFxRambr_(0), superFxColor_(0), superFxAlt_(0),
      superFxRegisterLatch_(0),
      superFxSourceRegister_(0), superFxDestinationRegister_(0), superFxRomBuffer_(0),
      superFxScreenBase_(0), superFxScreenMode_(0), superFxConfig_(0),
      superFxClockSelect_(0), superFxPlotOption_(0), superFxCacheBase_(0),
      superFxLastRamAddress_(0), superFxBranchPending_(false), superFxBranchTarget_(0),
      superFxBankPending_(false), superFxBankTarget_(0),
      superFxClockRemainder_(0),
      superFxCache_(512u, 0), superFxCacheValid_(32u, 0),
      superFxRam_(0x10000u, 0), superFxTraceAddress_(), superFxTraceOpcode_(),
      superFxTracePosition_(0), superFxTraceCount_(0),
      sdd1Regs_(), sdd1DmaActive_(false),
      sdd1StreamInitialized_(false), sdd1DmaChannel_(0), sdd1DmaSource_(0),
      sdd1DmaRemaining_(0), sdd1InputBitPosition_(0), sdd1OutputBytePosition_(0),
      sdd1BitplaneMode_(0), sdd1ContextMode_(0), sdd1NumPlanes_(2),
      sdd1Plane_(0), sdd1YLocation_(0), sdd1History_(),
      sdd1ContextState_(), sdd1ContextMps_(), sdd1RunCount_(),
      sdd1RunHasLps_(), sdd1RunActive_(),
      spc7110Regs_(0x43u, 0), spc7110DataPointer_(0), spc7110DecompPointer_(0),
      spc7110DecompLength_(0), spc7110MathResult_(0), spc7110Remainder_(0),
      spc7110DecompInitialized_(false), spc7110DecompMode_(0),
      spc7110DecompOutput_(0), spc7110DecompBufferIndex_(0),
      spc7110DecompLastPixel_(0), spc7110DecompTop_(255),
      spc7110DecompValue_(0), spc7110DecompInputBits_(0),
      spc7110PixelOrder_(), spc7110RealOrder_(), spc7110ContextIndex_(),
      spc7110ContextInvert_(), spc7110BitplaneBuffer_(),
      spc7110DataPortActive_(false), spc7110OffsetWriteMask_(0),
      spc7110RtcRegisters_(), spc7110RtcState_(0), spc7110RtcIndex_(0),
      spc7110RtcLastUpdate_(0), spc7110RtcClockRemainder_(0) {
    sa1Cpu_.connectBus(this);
    reset();
}

void EnhancementChip::rebindRuntime() {
    sa1Cpu_.connectBus(this);
    sa1ActiveRom_ = 0;
    sa1ActiveBwram_ = 0;
    sa1ActiveRamDirty_ = 0;
}

void EnhancementChip::configure(Kind kind, u8 mapMode, u8 cartridgeType,
                                const std::string& title, std::size_t romSize,
                                std::size_t cartridgeRamSize) {
    kind_ = kind;
    mapMode_ = mapMode;
    cartridgeType_ = cartridgeType;
    title_ = title;
    romSize_ = romSize;
    dspVariant_ = 1;
    if (title.find("DUNGEON MASTER") != std::string::npos) dspVariant_ = 2;
    else if (title.find("SD GUNDAM GX") != std::string::npos) dspVariant_ = 3;
    else if (title.find("TOP GEAR 3000") != std::string::npos ||
             title.find("PLANETS CHAMP") != std::string::npos) dspVariant_ = 4;
    if (kind_ == SuperFX) {
        // GSU-2 cartridges (type $15 and later) expose two 64 KiB work-RAM
        // banks even though the standard header SRAM-size byte is commonly 0.
        const std::size_t hardwareRam = cartridgeType_ >= 0x15u ? 0x20000u : 0x10000u;
        std::size_t size = std::max<std::size_t>(hardwareRam, cartridgeRamSize);
        if (size > 0x20000u) size = 0x20000u;
        superFxRam_.assign(size, 0);
    }
    reset();
}

void EnhancementChip::reset() {
    operations_ = 0;
    dspCommand_ = 0;
    dspExpected_ = 0;
    dspStage_ = 0;
    dspTransparent_ = 0;
    dspLengthA_ = 0;
    dspLengthB_ = 0;
    dspInput_.clear();
    dspOutput_.clear();
    std::fill(cx4Ram_.begin(), cx4Ram_.end(), 0);
    std::fill(sa1Iram_.begin(), sa1Iram_.end(), 0);
    std::fill(sa1Regs_.begin(), sa1Regs_.end(), 0);
    // Nintendo specifies CCNT=$20 at power-on: the embedded CPU is held in
    // reset until the S-CPU supplies CRV/CNV/CIV and clears RESB.
    sa1Regs_[0x00u] = 0x20u;
    sa1Regs_[0x20u] = 0x00u;
    sa1Regs_[0x21u] = 0x01u;
    sa1Regs_[0x22u] = 0x02u;
    sa1Regs_[0x23u] = 0x03u;
    sa1Regs_[0x28u] = 0x0Fu;
    sa1ArithmeticResult_ = 0;
    sa1Overflow_ = false;
    sa1ActiveRom_ = 0;
    sa1ActiveBwram_ = 0;
    sa1ActiveRamDirty_ = 0;
    sa1ClockRemainder_ = 0;
    sa1Running_ = false;
    sa1TimerClocks_ = 0;
    sa1VariableAddress_ = 0;
    sa1VariableBitOffset_ = 0;
    sa1LatchedH_ = sa1LatchedV_ = 0;
    sa1Cc2Line_ = sa1Cc2Tile_ = 0;
    sa1Cpu_.reset();
    for (unsigned i = 0; i < 16u; ++i) superFxR_[i] = 0;
    superFxStatus_ = 0;
    superFxPbr_ = superFxRombr_ = superFxRambr_ = superFxColor_ = superFxAlt_ = 0;
    superFxRegisterLatch_ = 0;
    superFxSourceRegister_ = superFxDestinationRegister_ = 0;
    superFxRomBuffer_ = superFxScreenBase_ = superFxScreenMode_ = 0;
    superFxConfig_ = superFxClockSelect_ = superFxPlotOption_ = 0;
    superFxCacheBase_ = superFxLastRamAddress_ = 0;
    superFxBranchPending_ = false;
    superFxBranchTarget_ = 0;
    superFxBankPending_ = false;
    superFxBankTarget_ = 0;
    superFxClockRemainder_ = 0;
    std::fill(superFxCache_.begin(), superFxCache_.end(), 0);
    std::fill(superFxCacheValid_.begin(), superFxCacheValid_.end(), 0);
    std::fill(superFxRam_.begin(), superFxRam_.end(), 0);
    superFxTracePosition_=superFxTraceCount_=0u;
    for (unsigned i = 0; i < 8u; ++i) sdd1Regs_[i] = i < 4u ? 0u : static_cast<u8>(i - 4u);
    sdd1DmaActive_ = false;
    sdd1StreamInitialized_ = false;
    sdd1DmaChannel_ = 0;
    sdd1DmaSource_ = 0;
    sdd1DmaRemaining_ = 0;
    sdd1InputBitPosition_ = 0;
    sdd1OutputBytePosition_ = 0;
    sdd1BitplaneMode_ = sdd1ContextMode_ = 0;
    sdd1NumPlanes_ = 2;
    sdd1Plane_ = sdd1YLocation_ = 0;
    for (unsigned i = 0; i < 8u; ++i) { sdd1History_[i] = 0; sdd1RunCount_[i] = 0; sdd1RunHasLps_[i] = false; sdd1RunActive_[i] = false; }
    for (unsigned i = 0; i < 32u; ++i) { sdd1ContextState_[i] = 0; sdd1ContextMps_[i] = 0; }
    std::fill(spc7110Regs_.begin(), spc7110Regs_.end(), 0);
    spc7110Regs_[0x31u] = 0;
    spc7110Regs_[0x32u] = 1;
    spc7110Regs_[0x33u] = 2;
    spc7110DataPointer_ = 0;
    spc7110DecompPointer_ = 0;
    spc7110DecompLength_ = 0;
    spc7110MathResult_ = 0;
    spc7110Remainder_ = 0;
    spc7110DecompInitialized_ = false;
    spc7110DecompMode_ = 0;
    spc7110DecompOutput_ = 0;
    spc7110DecompBufferIndex_ = 0;
    spc7110DecompLastPixel_ = 0;
    spc7110DecompTop_ = 255;
    spc7110DecompValue_ = 0;
    spc7110DecompInputBits_ = 0;
    for (unsigned i = 0; i < 16u; ++i) {
        spc7110PixelOrder_[i] = static_cast<u8>(i);
        spc7110RealOrder_[i] = static_cast<u8>(i);
        spc7110BitplaneBuffer_[i] = 0;
        spc7110RtcRegisters_[i] = 0;
    }
    for (unsigned i = 0; i < 32u; ++i) {
        spc7110ContextIndex_[i] = 0;
        spc7110ContextInvert_[i] = 0;
    }
    spc7110DataPortActive_ = false;
    spc7110OffsetWriteMask_ = 0;
    spc7110RtcState_ = 0;
    spc7110RtcIndex_ = 0;
    spc7110RtcLastUpdate_ = std::time(0);
    spc7110RtcClockRemainder_ = 0;
}

s16 EnhancementChip::clamp16(s32 value) {
    return static_cast<s16>(std::max<s32>(-32768, std::min<s32>(32767, value)));
}

s32 EnhancementChip::sign24(u32 value) {
    value &= 0xFFFFFFu;
    return (value & 0x800000u) ? static_cast<s32>(value | 0xFF000000u) : static_cast<s32>(value);
}

u16 EnhancementChip::read16(const std::vector<u8>& m, std::size_t a) {
    if (m.empty()) return 0;
    return static_cast<u16>(m[a % m.size()] | (static_cast<u16>(m[(a + 1u) % m.size()]) << 8));
}

u32 EnhancementChip::read24(const std::vector<u8>& m, std::size_t a) {
    if (m.empty()) return 0;
    return static_cast<u32>(m[a % m.size()]) |
        (static_cast<u32>(m[(a + 1u) % m.size()]) << 8) |
        (static_cast<u32>(m[(a + 2u) % m.size()]) << 16);
}

void EnhancementChip::write16(std::vector<u8>& m, std::size_t a, u16 v) {
    if (m.empty()) return;
    m[a % m.size()] = static_cast<u8>(v);
    m[(a + 1u) % m.size()] = static_cast<u8>(v >> 8);
}
void EnhancementChip::write24(std::vector<u8>& m, std::size_t a, u32 v) {
    if (m.empty()) return;
    for (unsigned i = 0; i < 3u; ++i) m[(a + i) % m.size()] = static_cast<u8>(v >> (i * 8u));
}
void EnhancementChip::write48(std::vector<u8>& m, std::size_t a, s64 v) {
    if (m.empty()) return;
    for (unsigned i = 0; i < 6u; ++i) m[(a + i) % m.size()] = static_cast<u8>(static_cast<u64>(v) >> (i * 8u));
}

bool EnhancementChip::isDspPort(u32 address) const {
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 offset = static_cast<u16>(address);
    const bool system = bank <= 0x3Fu || (bank >= 0x80u && bank <= 0xBFu);
    if (system && offset >= 0x6000u && offset <= 0x7FFFu) return true;
    return ((bank >= 0x20u && bank <= 0x3Fu) || (bank >= 0xA0u && bank <= 0xBFu)) && offset >= 0x8000u;
}

unsigned EnhancementChip::dspExpectedBytes(u8 command) const {
    if (dspVariant_ == 2u) {
        switch (command) {
            case 0x01: return 32;
            case 0x03: return 1;
            case 0x05: return 1;
            case 0x06: return 1;
            case 0x09: return 4;
            case 0x0D: return 2;
            default: return 0;
        }
    }
    switch (command & 0x3Fu) {
        case 0x00: return 4; // multiply
        case 0x04: return 4; // sin/cos
        case 0x08: return 6; // vector magnitude squared
        case 0x0A: return 2; // raster
        case 0x0C: return 6; // rotate 2D
        case 0x10: return 4; // reciprocal
        case 0x18: return 8; // range
        case 0x1C: return 12; // rotate 3D
        case 0x28: return 6; // vector length
        default: return 0;
    }
}

void EnhancementChip::pushDspWord(s16 value) {
    dspOutput_.push_back(static_cast<u8>(value));
    dspOutput_.push_back(static_cast<u8>(static_cast<u16>(value) >> 8));
}
void EnhancementChip::pushDspUnsigned(u16 value) {
    dspOutput_.push_back(static_cast<u8>(value));
    dspOutput_.push_back(static_cast<u8>(value >> 8));
}

bool EnhancementChip::executeDspCommand() {
    const u8 c = dspCommand_;
    const std::vector<u8>& b = dspInput_;
    const struct Word { static s16 get(const std::vector<u8>& v, unsigned o) {
        return static_cast<s16>(static_cast<u16>(v[o] | (static_cast<u16>(v[o + 1u]) << 8))); } } word;
    dspOutput_.clear();

    if (dspVariant_ == 2u) {
        switch (c) {
            case 0x01: { // packed 4bpp bitmap to SNES planar tile
                if (b.size() < 32u) return true;
                u8 firstHalf[16];
                u8 secondHalf[16];
                for (unsigned block = 0; block < 8u; ++block) {
                    const u8 c0 = b[block * 4u + 0u];
                    const u8 c1 = b[block * 4u + 1u];
                    const u8 c2 = b[block * 4u + 2u];
                    const u8 c3 = b[block * 4u + 3u];
                    firstHalf[block * 2u + 0u] = static_cast<u8>(
                        ((c0 & 0x10u) << 3) | ((c0 & 0x01u) << 6) |
                        ((c1 & 0x10u) << 1) | ((c1 & 0x01u) << 4) |
                        ((c2 & 0x10u) >> 1) | ((c2 & 0x01u) << 2) |
                        ((c3 & 0x10u) >> 3) |  (c3 & 0x01u));
                    firstHalf[block * 2u + 1u] = static_cast<u8>(
                        ((c0 & 0x20u) << 2) | ((c0 & 0x02u) << 5) |
                         (c1 & 0x20u)       | ((c1 & 0x02u) << 3) |
                        ((c2 & 0x20u) >> 2) | ((c2 & 0x02u) << 1) |
                        ((c3 & 0x20u) >> 4) | ((c3 & 0x02u) >> 1));
                    secondHalf[block * 2u + 0u] = static_cast<u8>(
                        ((c0 & 0x40u) << 1) | ((c0 & 0x04u) << 4) |
                        ((c1 & 0x40u) >> 1) | ((c1 & 0x04u) << 2) |
                        ((c2 & 0x40u) >> 3) |  (c2 & 0x04u)       |
                        ((c3 & 0x40u) >> 5) | ((c3 & 0x04u) >> 2));
                    secondHalf[block * 2u + 1u] = static_cast<u8>(
                         (c0 & 0x80u)       | ((c0 & 0x08u) << 3) |
                        ((c1 & 0x80u) >> 2) | ((c1 & 0x08u) << 1) |
                        ((c2 & 0x80u) >> 4) | ((c2 & 0x08u) >> 1) |
                        ((c3 & 0x80u) >> 6) | ((c3 & 0x08u) >> 3));
                }
                for (unsigned i = 0; i < 16u; ++i) dspOutput_.push_back(firstHalf[i]);
                for (unsigned i = 0; i < 16u; ++i) dspOutput_.push_back(secondHalf[i]);
                break;
            }
            case 0x03:
                if (!b.empty()) dspTransparent_ = static_cast<u8>(b[0] & 0x0Fu);
                break;
            case 0x05:
                if (dspStage_ == 0u) {
                    dspLengthA_ = b.empty() ? 0u : b[0];
                    dspStage_ = 1u;
                    dspExpected_ = dspLengthA_ * 2u;
                    return dspLengthA_ == 0u;
                } else {
                    const unsigned length = std::min<unsigned>(dspLengthA_, static_cast<unsigned>(b.size() / 2u));
                    for (unsigned i = 0; i < length; ++i) {
                        const u8 beneath = b[i];
                        const u8 above = b[dspLengthA_ + i];
                        const u8 high = ((above >> 4) == dspTransparent_) ? (beneath & 0xF0u) : (above & 0xF0u);
                        const u8 low = ((above & 0x0Fu) == dspTransparent_) ? (beneath & 0x0Fu) : (above & 0x0Fu);
                        dspOutput_.push_back(static_cast<u8>(high | low));
                    }
                }
                break;
            case 0x06:
                if (dspStage_ == 0u) {
                    dspLengthA_ = b.empty() ? 0u : b[0];
                    dspStage_ = 1u;
                    dspExpected_ = dspLengthA_;
                    return dspLengthA_ == 0u;
                } else {
                    const unsigned length = std::min<unsigned>(dspLengthA_, static_cast<unsigned>(b.size()));
                    for (unsigned i = 0; i < length; ++i) {
                        const u8 value = b[length - 1u - i];
                        dspOutput_.push_back(static_cast<u8>((value << 4) | (value >> 4)));
                    }
                }
                break;
            case 0x09: {
                if (b.size() < 4u) break;
                const u32 product = static_cast<u32>(static_cast<u16>(word.get(b, 0))) *
                                    static_cast<u32>(static_cast<u16>(word.get(b, 2)));
                for (unsigned i = 0; i < 4u; ++i) dspOutput_.push_back(static_cast<u8>(product >> (i * 8u)));
                break;
            }
            case 0x0D:
                if (dspStage_ == 0u) {
                    dspLengthA_ = b.size() > 0u ? b[0] : 0u; // source pixels
                    dspLengthB_ = b.size() > 1u ? b[1] : 0u; // destination bytes
                    dspStage_ = 1u;
                    dspExpected_ = (dspLengthA_ + 1u) >> 1;
                    return dspLengthA_ == 0u || dspLengthB_ == 0u;
                } else {
                    const unsigned outputBytes = std::min<unsigned>(dspLengthB_, 255u);
                    std::vector<u8> pixels(outputBytes * 2u, 0);
                    const u32 multiplier = dspLengthA_ <= dspLengthB_ ? 0x10000u :
                        static_cast<u32>((static_cast<u64>(dspLengthA_) << 17) /
                                         ((static_cast<u64>(dspLengthB_) << 1) + 1u));
                    u32 pixelPosition = 0;
                    for (unsigned i = 0; i < outputBytes * 2u; ++i) {
                        const unsigned sourcePixel = pixelPosition >> 16;
                        const unsigned byteIndex = sourcePixel >> 1;
                        if (byteIndex < b.size())
                            pixels[i] = (sourcePixel & 1u) ? static_cast<u8>(b[byteIndex] & 0x0Fu)
                                                         : static_cast<u8>(b[byteIndex] >> 4);
                        pixelPosition += multiplier;
                    }
                    for (unsigned i = 0; i < outputBytes; ++i)
                        dspOutput_.push_back(static_cast<u8>((pixels[i * 2u] << 4) | pixels[i * 2u + 1u]));
                }
                break;
            default:
                break;
        }
        ++operations_;
        dspStage_ = 0u;
        return true;
    }

    switch (c & 0x3Fu) {
        case 0x00: {
            const s32 result = (static_cast<s32>(word.get(b, 0)) * word.get(b, 2)) >> 15;
            pushDspWord(clamp16(result)); break;
        }
        case 0x04: {
            const double angle = static_cast<double>(static_cast<u16>(word.get(b, 0))) * (2.0 * Pi / 65536.0);
            const s32 radius = word.get(b, 2);
            pushDspWord(clamp16(static_cast<s32>(std::sin(angle) * radius)));
            pushDspWord(clamp16(static_cast<s32>(std::cos(angle) * radius)));
            break;
        }
        case 0x08: {
            const s64 x = word.get(b, 0), y = word.get(b, 2), z = word.get(b, 4);
            const u32 value = static_cast<u32>(std::min<s64>(0xFFFFFFFFll, (x*x + y*y + z*z) << 1));
            pushDspUnsigned(static_cast<u16>(value)); pushDspUnsigned(static_cast<u16>(value >> 16));
            break;
        }
        case 0x0A: pushDspWord(0); pushDspWord(0); pushDspWord(0); pushDspWord(0); break;
        case 0x0C: {
            const double a = static_cast<double>(static_cast<u16>(word.get(b, 0))) * (2.0 * Pi / 65536.0);
            const s32 x = word.get(b, 2), y = word.get(b, 4);
            pushDspWord(clamp16(static_cast<s32>(x * std::cos(a) + y * std::sin(a))));
            pushDspWord(clamp16(static_cast<s32>(y * std::cos(a) - x * std::sin(a))));
            break;
        }
        case 0x10: {
            const s16 coefficient = word.get(b, 0);
            s16 exponent = word.get(b, 2);
            if (coefficient == 0) { pushDspWord(0x7FFF); pushDspWord(0x002F); }
            else {
                double v = 32768.0 / static_cast<double>(std::abs(static_cast<int>(coefficient)));
                while (v > 32767.0) { v *= 0.5; --exponent; }
                s16 out = clamp16(static_cast<s32>(v));
                if (coefficient < 0) out = static_cast<s16>(-out);
                pushDspWord(out); pushDspWord(exponent);
            }
            break;
        }
        case 0x18: {
            const s64 x = word.get(b,0), y = word.get(b,2), z = word.get(b,4), r = word.get(b,6);
            const s64 result = ((x*x + y*y + z*z - r*r) >> 15);
            pushDspWord(clamp16(static_cast<s32>(result))); break;
        }
        case 0x1C: {
            double x=word.get(b,6), y=word.get(b,8), z=word.get(b,10);
            const double az=static_cast<u16>(word.get(b,0))*2.0*Pi/65536.0;
            const double ay=static_cast<u16>(word.get(b,2))*2.0*Pi/65536.0;
            const double ax=static_cast<u16>(word.get(b,4))*2.0*Pi/65536.0;
            double nx=x*std::cos(az)+y*std::sin(az), ny=y*std::cos(az)-x*std::sin(az); x=nx; y=ny;
            nx=x*std::cos(ay)-z*std::sin(ay); double nz=x*std::sin(ay)+z*std::cos(ay); x=nx; z=nz;
            ny=y*std::cos(ax)+z*std::sin(ax); nz=z*std::cos(ax)-y*std::sin(ax);
            pushDspWord(clamp16(static_cast<s32>(x))); pushDspWord(clamp16(static_cast<s32>(ny))); pushDspWord(clamp16(static_cast<s32>(nz))); break;
        }
        case 0x28: {
            const double x=word.get(b,0), y=word.get(b,2), z=word.get(b,4);
            pushDspWord(clamp16(static_cast<s32>(std::sqrt(x*x+y*y+z*z)))); break;
        }
        default: break;
    }
    ++operations_;
    return true;
}

// CX4 has two CPU-visible mirrors of its 3 KiB data RAM, followed by a
// sparse control-register area. The $7F80-$7FAF operand register bank is
// also visible at $7FC0-$7FEF; the intervening 16-byte gaps read as zero.
// Keep the historic 8 KiB state allocation for save-state compatibility.
bool EnhancementChip::readCx4(u32 address, u8& value) const {
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 off = static_cast<u16>(address);
    if (!((bank <= 0x3Fu || (bank >= 0x80u && bank <= 0xBFu)) &&
          off >= 0x6000u && off <= 0x7FFFu)) return false;

    if (off >= 0x6000u && off < 0x6C00u) { value = cx4Ram_[off - 0x6000u]; return true; }
    if (off >= 0x7000u && off < 0x7C00u) { value = cx4Ram_[off - 0x7000u]; return true; }
    if (off >= 0x7F80u) {
        const unsigned reg = off & 0x3Fu;
        value = reg < 0x30u ? cx4Ram_[0x1F80u + reg] : 0u;
        return true;
    }
    value = cx4Ram_[off - 0x6000u];
    if (off == 0x7F48u) value &= 0x01u;
    if (off == 0x7F4Cu) value &= 0x03u;
    if (off == 0x7F4Eu) value &= 0x7Fu;
    if (off == 0x7F50u) value &= 0x77u;
    if (off == 0x7F51u || off == 0x7F52u) value &= 0x01u;
    return true;
}

void EnhancementChip::executeCx4Command(u8 command) {
    const s32 x=sign24(read24(cx4Ram_,0x1F80u));
    const s32 y=sign24(read24(cx4Ram_,0x1F83u));
    switch(command) {
        case 0x05: // proportional wireframe vector
        case 0x0D: {
            const double length=std::sqrt(static_cast<double>(x)*x+static_cast<double>(y)*y);
            const s32 desired=static_cast<s16>(read16(cx4Ram_,0x1F86u));
            if(length>0.0){ write24(cx4Ram_,0x1F80u,static_cast<u32>(static_cast<s32>(x*desired/length))); write24(cx4Ram_,0x1F83u,static_cast<u32>(static_cast<s32>(y*desired/length))); }
            break;
        }
        case 0x10: case 0x13: {
            const double angle=static_cast<double>(read16(cx4Ram_,0x1F80u)&0x1FFu)*2.0*Pi/512.0;
            const s32 radius=static_cast<s16>(read16(cx4Ram_,0x1F83u));
            write24(cx4Ram_,0x1F86u,static_cast<u32>(static_cast<s32>(std::cos(angle)*radius)));
            write24(cx4Ram_,0x1F89u,static_cast<u32>(static_cast<s32>(std::sin(angle)*radius)));
            break;
        }
        case 0x15: write16(cx4Ram_,0x1F80u,static_cast<u16>(std::sqrt(static_cast<double>(x)*x+static_cast<double>(y)*y))); break;
        case 0x1F: {
            double a=std::atan2(static_cast<double>(y),static_cast<double>(x)); if(a<0)a+=2.0*Pi;
            write16(cx4Ram_,0x1F86u,static_cast<u16>(a*512.0/(2.0*Pi))&0x1FFu); break;
        }
        case 0x25: write48(cx4Ram_,0x1F80u,static_cast<s64>(x)*y); break;
        case 0x40: { u16 sum=0; for(unsigned i=0;i<0x800u;++i) sum=static_cast<u16>(sum+cx4Ram_[i]); write16(cx4Ram_,0x1F80u,sum); break; }
        case 0x54: write48(cx4Ram_,0x1F80u,static_cast<s64>(x)*x); break;
        default: break;
    }
    ++operations_;
}

bool EnhancementChip::writeCx4(u32 address, u8 value) {
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 off = static_cast<u16>(address);
    if (!((bank <= 0x3Fu || (bank >= 0x80u && bank <= 0xBFu)) &&
          off >= 0x6000u && off <= 0x7FFFu)) return false;

    if (off < 0x6C00u) { cx4Ram_[off - 0x6000u] = value; return true; }
    if (off >= 0x7000u && off < 0x7C00u) { cx4Ram_[off - 0x7000u] = value; return true; }
    if (off >= 0x7F80u) {
        const unsigned reg = off & 0x3Fu;
        if (reg < 0x30u) cx4Ram_[0x1F80u + reg] = value;
        return true;
    }
    const std::size_t index = off - 0x6000u;
    if (off == 0x7F48u) value &= 0x01u;
    if (off == 0x7F4Cu) value &= 0x03u;
    if (off == 0x7F4Eu) value &= 0x7Fu;
    if (off == 0x7F50u) value &= 0x77u;
    if (off == 0x7F51u || off == 0x7F52u) value &= 0x01u;
    cx4Ram_[index] = value;
    if (index == 0x1F4Fu) executeCx4Command(value);
    return true;
}

bool EnhancementChip::readSa1(u32 address, const std::vector<u8>&, std::vector<u8>& cartridgeRam, u8& value) {
    const u8 bank=static_cast<u8>(address>>16); const u16 off=static_cast<u16>(address);
    const bool system=bank<=0x3Fu||(bank>=0x80u&&bank<=0xBFu);
    if ((bank & 0x7Fu) == 0u) {
        // SCNT can redirect the S-CPU's NMI/IRQ vectors to the vectors written
        // by the SA-1 without altering the ROM-resident table.
        if ((sa1Regs_[0x09u] & 0x10u) &&
            (off == 0xFFEAu || off == 0xFFEBu || off == 0xFFFAu || off == 0xFFFBu)) {
            value = sa1Regs_[0x0Cu + (off & 1u)]; return true;
        }
        if ((sa1Regs_[0x09u] & 0x40u) &&
            (off == 0xFFEEu || off == 0xFFEFu || off == 0xFFFEu || off == 0xFFFFu)) {
            value = sa1Regs_[0x0Eu + (off & 1u)]; return true;
        }
    }
    if(system&&off>=0x2300u&&off<=0x23FFu) {
        const unsigned r=off-0x2200u;
        if(off==0x2300u) value=static_cast<u8>((sa1Regs_[0x09u]&0x5Fu)|(sa1Regs_[0x100u]&0xA0u));
        else if(off==0x2301u) value=static_cast<u8>((sa1Regs_[0x00u]&0x0Fu)|(sa1Regs_[0x101u]&0xF0u));
        else if(off>=0x2306u&&off<=0x230Au) value=static_cast<u8>(static_cast<u64>(sa1ArithmeticResult_)>>((off-0x2306u)*8u));
        else if(off==0x230Bu) value=sa1Overflow_?0x80u:0u;
        else value=sa1Regs_[r&0x1FFu];
        return true;
    }
    if(system&&off>=0x3000u&&off<=0x37FFu){value=sa1Iram_[off&0x7FFu];return true;}
    if(system&&off>=0x6000u&&off<=0x7FFFu){const std::vector<u8>& m=cartridgeRam.empty()?sa1Bwram_:cartridgeRam; value=m[sa1MapBwram(address,false,m.size())];return true;}
    if(bank>=0x40u&&bank<=0x4Fu){const std::vector<u8>& m=cartridgeRam.empty()?sa1Bwram_:cartridgeRam; value=m[sa1MapBwram(address,false,m.size())];return true;}
    return false;
}

void EnhancementChip::executeSa1Math() {
    const u8 mode=sa1Regs_[0x50u]&3u;
    const s16 a=static_cast<s16>(static_cast<u16>(sa1Regs_[0x51u]|(sa1Regs_[0x52u]<<8)));
    const u16 rawB=static_cast<u16>(sa1Regs_[0x53u]|(sa1Regs_[0x54u]<<8));
    if(mode==0u) sa1ArithmeticResult_=static_cast<s32>(a)*static_cast<s16>(rawB);
    else if(mode==1u){
        if(rawB==0u)sa1ArithmeticResult_=0;
        else{const s32 dividend=a;const s32 quotient=dividend/static_cast<s32>(rawB);
            s32 remainder=dividend%static_cast<s32>(rawB);if(remainder<0)remainder+=rawB;
            sa1ArithmeticResult_=(static_cast<s64>(static_cast<u16>(remainder))<<16)|static_cast<u16>(quotient);}
    }else if(mode==2u){
        s64 sum=sa1ArithmeticResult_+static_cast<s32>(a)*static_cast<s16>(rawB);
        if(sum>0x7FFFFFFFFFll||sum<static_cast<s64>(-0x8000000000ll))sa1Overflow_=true;
        const u64 wrapped=static_cast<u64>(sum)&0xFFFFFFFFFFull;
        sa1ArithmeticResult_=(wrapped&0x8000000000ull)?static_cast<s64>(wrapped|0xFFFFFF0000000000ull):static_cast<s64>(wrapped);
    }
    ++operations_;
}

void EnhancementChip::executeSa1Dma(const std::vector<u8>& rom, std::vector<u8>& cartridgeRam, bool& ramDirty) {
    const u8 control=sa1Regs_[0x30u];
    if((control&0xA0u)!=0x80u)return;
    const unsigned sourceDevice=control&3u;
    const bool destinationBwram=(control&4u)!=0u;
    if(sourceDevice==3u||(sourceDevice==1u&&destinationBwram)||(sourceDevice==2u&&!destinationBwram))return;
    const u32 src=make24(sa1Regs_,0x32u), dst=make24(sa1Regs_,0x35u);
    const u16 count=static_cast<u16>(sa1Regs_[0x38u]|(sa1Regs_[0x39u]<<8));
    std::vector<u8>& bwram=cartridgeRam.empty()?sa1Bwram_:cartridgeRam;
    for(unsigned i=0;i<count;++i){
        u8 value=0xFFu;
        if(sourceDevice==0u&&!rom.empty())value=rom[sa1MapRom(src+i,rom.size())];
        else if(sourceDevice==1u&&!bwram.empty())value=bwram[mirror((src+i)&0x3FFFFu,bwram.size())];
        else if(sourceDevice==2u)value=sa1Iram_[(src+i)&0x7FFu];
        if(destinationBwram&&!bwram.empty()){bwram[mirror((dst+i)&0x3FFFFu,bwram.size())]=value;ramDirty=true;}
        else sa1Iram_[(dst+i)&0x7FFu]=value;
    }
    sa1Regs_[0x101u]|=0x20u;
    updateSa1IrqLine();
    ++operations_;
}

void EnhancementChip::executeSa1CharacterConversion2(bool& ramDirty){
    const u8 control=sa1Regs_[0x30u];
    if((control&0xB0u)!=0xA0u)return;
    const unsigned depthCode=sa1Regs_[0x31u]&3u;
    if(depthCode==3u)return;
    const unsigned bpp=depthCode==0u?8u:(depthCode==1u?4u:2u);
    const unsigned row=sa1Cc2Line_&7u;
    const unsigned registerBase=(row&1u)*8u;
    const unsigned bytesPerTile=bpp*8u;
    const unsigned destination=(static_cast<unsigned>(sa1Regs_[0x35u]|(sa1Regs_[0x36u]<<8))&0x7FFu)+
        (sa1Cc2Tile_&1u)*bytesPerTile;
    for(unsigned plane=0;plane<bpp;++plane){
        u8 packed=0;
        for(unsigned x=0;x<8u;++x)packed|=static_cast<u8>(((sa1Regs_[0x40u+registerBase+x]>>plane)&1u)<<(7u-x));
        sa1Iram_[(destination+(plane>>1u)*16u+row*2u+(plane&1u))&0x7FFu]=packed;
    }
    if(++sa1Cc2Line_==8u){sa1Cc2Line_=0u;++sa1Cc2Tile_;sa1Regs_[0x100u]|=0x20u;}
    ramDirty=true;
    ++operations_;
}

void EnhancementChip::updateSa1IrqLine(){
    const u8 pending=sa1Regs_[0x101u]&sa1Regs_[0x0Au]&0xE0u;
    sa1Cpu_.setIrqLine(pending!=0u);
}

bool EnhancementChip::writeSa1(u32 address,u8 value,const std::vector<u8>& rom,std::vector<u8>& cartridgeRam,bool& ramDirty){
    const u8 bank=static_cast<u8>(address>>16);const u16 off=static_cast<u16>(address);const bool system=bank<=0x3Fu||(bank>=0x80u&&bank<=0xBFu);
    if(system&&off>=0x2200u&&off<=0x22FFu){
        // CIWP ($222A) and CBWE ($2227) are SA-1-side-only writes.
        // The S-CPU must not alter the coprocessor's own protection gates.
        if (off == 0x2227u || off == 0x222Au) return true;
        const unsigned r=off-0x2200u;const u8 old=sa1Regs_[r];sa1Regs_[r]=value;
        if(off==0x2200u){
            if(value&0x80u)sa1Regs_[0x101u]|=0x80u;
            if((value&0x10u)!=0u){sa1Regs_[0x101u]|=0x10u;if(sa1Regs_[0x0Au]&0x10u)sa1Cpu_.requestNmi();}
            if(value&0x80u)sa1Regs_[0x101u]|=0x80u;
            if((old&0x20u)!=0u&&(value&0x20u)==0u){
                // Releasing SA-1 reset clears the coprocessor-owned I-RAM
                // write protection. SNES-side SIWP must be retained.
                sa1Regs_[0x2Au] = 0u;
                sa1ActiveRom_=&rom;sa1ActiveBwram_=cartridgeRam.empty()?&sa1Bwram_:&cartridgeRam;sa1ActiveRamDirty_=&ramDirty;
                sa1Cpu_.reset();sa1Running_=true;
                sa1ActiveRom_=0;sa1ActiveBwram_=0;sa1ActiveRamDirty_=0;
            }
            if(value&0x20u)sa1Running_=false;
            updateSa1IrqLine();
        }
        if(off==0x2202u)sa1Regs_[0x100u]&=static_cast<u8>(~(value&0xA0u));
        if(off==0x2211u)sa1TimerClocks_=0;
        if(off==0x2230u){sa1Cc2Line_=sa1Cc2Tile_=0;}
        if(off>=0x2259u&&off<=0x225Bu){sa1VariableAddress_=make24(sa1Regs_,0x59u);sa1VariableBitOffset_=0;}
        if(off==0x2250u&&value&2u){sa1ArithmeticResult_=0;sa1Overflow_=false;}
        if(off==0x2254u)executeSa1Math();
        if((off==0x2236u&&(sa1Regs_[0x30u]&4u)==0u)||
           (off==0x2237u&&(sa1Regs_[0x30u]&4u)!=0u))executeSa1Dma(rom,cartridgeRam,ramDirty);
        if(off>=0x2240u&&off<=0x224Fu&&((off&7u)==7u))executeSa1CharacterConversion2(ramDirty);
        return true;}
    if(system&&off>=0x3000u&&off<=0x37FFu){const unsigned block=(off&0x7FFu)>>8;if(sa1Regs_[0x29u]&(1u<<block))sa1Iram_[off&0x7FFu]=value;return true;}
    if((system&&off>=0x6000u&&off<=0x7FFFu)||(bank>=0x40u&&bank<=0x4Fu)){std::vector<u8>& m=cartridgeRam.empty()?sa1Bwram_:cartridgeRam;const std::size_t mapped=sa1MapBwram(address,false,m.size());if(sa1BwramWriteAllowed(sa1Regs_[0x26u],sa1Regs_[0x27u],sa1Regs_[0x28u],
    sa1BwramProtectionAddress(address,false,sa1Regs_[0x24u],sa1Regs_[0x25u],sa1Regs_[0x3Fu]))){m[mapped]=value;ramDirty=true;}return true;}
    return false;
}

std::size_t EnhancementChip::sa1MapRom(u32 address, std::size_t romSize) const {
    if (romSize == 0u) return 0u;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 offset = static_cast<u16>(address);
    unsigned region = 0u;
    std::size_t within = 0u;
    if (bank >= 0xC0u) {
        region = (bank - 0xC0u) >> 4;
        within = (static_cast<std::size_t>(bank & 0x0Fu) << 16) | offset;
    } else {
        region = bank < 0x40u ? (bank >> 5) : 2u + ((bank - 0x80u) >> 5);
        const u8 mapping=sa1Regs_[0x20u+(region&3u)];
        if((mapping&0x80u)==0u){
            const std::size_t base=bank>=0x80u?0x200000u:0u;
            return mirror(base+(static_cast<std::size_t>(bank&0x3Fu)<<15)+
                          (offset&0x7FFFu),romSize);
        }
        within = (static_cast<std::size_t>(bank & 0x1Fu) << 15) |
                 (offset & 0x7FFFu);
    }
    const std::size_t page = static_cast<std::size_t>(sa1Regs_[0x20u + region] & 7u);
    return mirror((page << 20) | within, romSize);
}

std::size_t EnhancementChip::sa1MapBwram(u32 address, bool sa1Side,
                                          std::size_t ramSize) const {
    if (ramSize == 0u) return 0u;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 offset = static_cast<u16>(address);
    if (bank >= 0x40u && bank <= (sa1Side ? 0x5Fu : 0x4Fu))
        return mirror((static_cast<std::size_t>(bank - 0x40u) << 16) | offset, ramSize);
    if (sa1Side && bank >= 0x60u && bank <= 0x6Fu) {
        const std::size_t pixel = (static_cast<std::size_t>(bank - 0x60u) << 16) | offset;
        const bool fourColor = (sa1Regs_[0x3Fu] & 0x80u) != 0u;
        return mirror(pixel >> (fourColor ? 2u : 1u), ramSize);
    }
    const u8 map = sa1Regs_[sa1Side ? 0x25u : 0x24u];
    const std::size_t page = static_cast<std::size_t>(map & (sa1Side ? 0x7Fu : 0x1Fu));
    return mirror((page << 13) | (offset & 0x1FFFu), ramSize);
}

u8 EnhancementChip::read(u32 address) {
    address &= 0xFFFFFFu;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 off = static_cast<u16>(address);
    const bool system = bank <= 0x3Fu || (bank >= 0x80u && bank <= 0xBFu);

    // The SA-1 reset/NMI/IRQ vectors are supplied by S-CPU registers rather
    // than the vectors stored in the program ROM.
    if ((bank & 0x7Fu) == 0u) {
        if (off == 0xFFFCu || off == 0xFFFDu)
            return sa1Regs_[0x03u + (off - 0xFFFCu)];
        if (off == 0xFFEAu || off == 0xFFEBu || off == 0xFFFAu || off == 0xFFFBu)
            return sa1Regs_[0x05u + (off & 1u)];
        if (off == 0xFFEEu || off == 0xFFEFu || off == 0xFFFEu || off == 0xFFFFu)
            return sa1Regs_[0x07u + (off & 1u)];
    }
    if (system && off >= 0x2200u && off <= 0x23FFu) {
        if (off == 0x2300u) return static_cast<u8>((sa1Regs_[0x09u]&0x5Fu)|(sa1Regs_[0x100u]&0xA0u));
        if (off == 0x2301u) return static_cast<u8>((sa1Regs_[0x00u]&0x0Fu)|(sa1Regs_[0x101u]&0xF0u));
        if(off==0x2302u){const bool linear=(sa1Regs_[0x10u]&0x80u)!=0u;const u32 ticks=sa1TimerClocks_/(linear?2u:4u);sa1LatchedH_=static_cast<u16>(ticks%(linear?512u:341u));sa1LatchedV_=static_cast<u16>((ticks/(linear?512u:341u))%(linear?512u:262u));return static_cast<u8>(sa1LatchedH_);}
        if(off==0x2303u)return static_cast<u8>(sa1LatchedH_>>8);
        if(off==0x2304u)return static_cast<u8>(sa1LatchedV_);
        if(off==0x2305u)return static_cast<u8>(sa1LatchedV_>>8);
        if (off >= 0x2306u && off <= 0x230Au)
            return static_cast<u8>(static_cast<u64>(sa1ArithmeticResult_) >> ((off - 0x2306u) * 8u));
        if (off == 0x230Bu) return sa1Overflow_ ? 0x80u : 0u;
        if (off == 0x230Cu || off == 0x230Du) {
            u32 bits = 0u;
            if (sa1ActiveRom_ && !sa1ActiveRom_->empty()) {
                for (unsigned i = 0; i < 3u; ++i) {
                    const u32 source = (sa1VariableAddress_ + i) & 0xFFFFFFu;
                    bits |= static_cast<u32>((*sa1ActiveRom_)[sa1MapRom(source, sa1ActiveRom_->size())]) << (i * 8u);
                }
            }
            bits >>= sa1VariableBitOffset_;
            const u8 result = static_cast<u8>(bits >> ((off - 0x230Cu) * 8u));
            if (off == 0x230Du && (sa1Regs_[0x58u] & 0x80u)) {
                unsigned count = sa1Regs_[0x58u] & 0x0Fu;
                if (count == 0u) count = 16u;
                sa1VariableBitOffset_ += count;
                sa1VariableAddress_ = (sa1VariableAddress_ + (sa1VariableBitOffset_ >> 3)) & 0xFFFFFFu;
                sa1VariableBitOffset_ &= 7u;
            }
            return result;
        }
        return sa1Regs_[(off - 0x2200u) & 0x1FFu];
    }
    if (system && (off <= 0x07FFu || (off >= 0x3000u && off <= 0x37FFu)))
        return sa1Iram_[off & 0x07FFu];
    if ((system && off >= 0x6000u && off <= 0x7FFFu) ||
        (bank >= 0x40u && bank <= 0x5Fu) || (bank >= 0x60u && bank <= 0x6Fu)) {
        const std::vector<u8>& ram = sa1ActiveBwram_ ? *sa1ActiveBwram_ : sa1Bwram_;
        const std::size_t mapped = sa1MapBwram(address, true, ram.size());
        if (bank >= 0x60u && bank <= 0x6Fu) {
            const u8 packed = ram[mapped];
            const unsigned pixel = static_cast<unsigned>(address & 0xFFFFFu);
            if (sa1Regs_[0x3Fu] & 0x80u) return static_cast<u8>((packed >> ((pixel & 3u) * 2u)) & 3u);
            return static_cast<u8>((packed >> ((pixel & 1u) * 4u)) & 15u);
        }
        return ram[mapped];
    }
    if (sa1ActiveRom_ && !sa1ActiveRom_->empty() &&
        ((system && off >= 0x8000u) || bank >= 0xC0u))
        return (*sa1ActiveRom_)[sa1MapRom(address, sa1ActiveRom_->size())];
    return 0xFFu;
}

void EnhancementChip::write(u32 address, u8 value) {
    address &= 0xFFFFFFu;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 off = static_cast<u16>(address);
    const bool system = bank <= 0x3Fu || (bank >= 0x80u && bank <= 0xBFu);
    if (system && off >= 0x2200u && off <= 0x22FFu) {
        // S-CPU-owned SWEN/SIWP/BWPA are not writable by the SA-1 CPU.
        if (off == 0x2226u || off == 0x2228u || off == 0x2229u) return;
        const unsigned r = off - 0x2200u;
        sa1Regs_[r] = value;
        if (off == 0x2209u) {
            if(value&0x80u)sa1Regs_[0x100u]|=0x80u;
        } else if (off == 0x220Bu) {
            sa1Regs_[0x101u] &= static_cast<u8>(~value);
            updateSa1IrqLine();
        } else if (off == 0x220Au) {
            if((value&0x10u)&&(sa1Regs_[0x101u]&0x10u))sa1Cpu_.requestNmi();
            updateSa1IrqLine();
        } else if (off == 0x2211u) {
            sa1TimerClocks_ = 0u;
        } else if (off == 0x2230u) {
            sa1Cc2Line_=sa1Cc2Tile_=0;
        } else if (off >= 0x2259u && off <= 0x225Bu) {
            sa1VariableAddress_ = make24(sa1Regs_, 0x59u);
            sa1VariableBitOffset_ = 0u;
        } else if (off == 0x2250u && (value&2u)) {
            sa1ArithmeticResult_=0;sa1Overflow_=false;
        } else if (off == 0x2254u) {
            executeSa1Math();
        } else if (((off==0x2236u&&(sa1Regs_[0x30u]&4u)==0u)||
                    (off==0x2237u&&(sa1Regs_[0x30u]&4u)!=0u))&&
                   sa1ActiveRom_ && sa1ActiveBwram_ && sa1ActiveRamDirty_) {
            executeSa1Dma(*sa1ActiveRom_, *sa1ActiveBwram_, *sa1ActiveRamDirty_);
        } else if (off>=0x2240u&&off<=0x224Fu&&((off&7u)==7u)&&sa1ActiveRamDirty_) {
            executeSa1CharacterConversion2(*sa1ActiveRamDirty_);
        }
        return;
    }
    if (system && (off <= 0x07FFu || (off >= 0x3000u && off <= 0x37FFu))) {
        const unsigned block = (off & 0x07FFu) >> 8;
        if (sa1Regs_[0x2Au] & (1u << block)) sa1Iram_[off & 0x07FFu] = value;
        return;
    }
    if (!sa1ActiveBwram_) return;
    if ((system && off >= 0x6000u && off <= 0x7FFFu) ||
        (bank >= 0x40u && bank <= 0x5Fu) || (bank >= 0x60u && bank <= 0x6Fu)) {
        const std::size_t mapped = sa1MapBwram(address, true, sa1ActiveBwram_->size());
        if (bank >= 0x60u && bank <= 0x6Fu) {
            u8& packed = (*sa1ActiveBwram_)[mapped];
            const unsigned pixel = static_cast<unsigned>(address & 0xFFFFFu);
            if (sa1Regs_[0x3Fu] & 0x80u) {
                const unsigned shift = (pixel & 3u) * 2u;
                packed = static_cast<u8>((packed & ~(3u << shift)) | ((value & 3u) << shift));
            } else {
                const unsigned shift = (pixel & 1u) * 4u;
                packed = static_cast<u8>((packed & ~(15u << shift)) | ((value & 15u) << shift));
            }
        } else if (sa1BwramWriteAllowed(sa1Regs_[0x26u],sa1Regs_[0x27u],
                                            sa1Regs_[0x28u],
                    sa1BwramProtectionAddress(address,true,sa1Regs_[0x24u],sa1Regs_[0x25u],sa1Regs_[0x3Fu]))) {
            (*sa1ActiveBwram_)[mapped] = value;
        }
        if (sa1ActiveRamDirty_) *sa1ActiveRamDirty_ = true;
    }
}

void EnhancementChip::stepSa1(unsigned masterClocks, const std::vector<u8>& rom,
                              std::vector<u8>& cartridgeRam, bool& ramDirty) {
    sa1TimerClocks_ += masterClocks;
    // The SA-1 linear timer advances at the 10.74 MHz CPU clock.  HV mode is
    // represented in the same master-clock domain so compare edges remain
    // deterministic even when the scheduler supplies arbitrary chunks.
    const u16 hTarget = static_cast<u16>(sa1Regs_[0x12u] | ((sa1Regs_[0x13u] & 1u) << 8));
    const u16 vTarget = static_cast<u16>(sa1Regs_[0x14u] | ((sa1Regs_[0x15u] & 1u) << 8));
    const u8 timerControl = sa1Regs_[0x10u];
    if ((timerControl & 3u) != 0u) {
        const bool linear=(timerControl&0x80u)!=0u;
        const u32 ticks=sa1TimerClocks_/(linear?2u:4u);
        const unsigned width=linear?512u:341u;
        const unsigned height=linear?512u:262u;
        const u16 h=static_cast<u16>(ticks%width);
        const u16 v=static_cast<u16>((ticks/width)%height);
        const bool match = ((timerControl & 1u) == 0u || h == hTarget) &&
                           ((timerControl & 2u) == 0u || v == vTarget);
        if (match) { sa1Regs_[0x101u] |= 0x40u; updateSa1IrqLine(); }
    }

    if (!sa1Running_ || (sa1Regs_[0x00u] & 0x60u) != 0u) return;
    sa1ActiveRom_ = &rom;
    sa1ActiveBwram_ = cartridgeRam.empty() ? &sa1Bwram_ : &cartridgeRam;
    sa1ActiveRamDirty_ = &ramDirty;
    sa1ClockRemainder_ += masterClocks;
    // One SA-1 cycle is two SNES master clocks. Instructions are indivisible;
    // a signed balance carries the final instruction's debt into later calls.
    unsigned guard = 0u;
    while (sa1ClockRemainder_ >= 2 && guard++ < 100000u) {
        const unsigned used = sa1Cpu_.step();
        const unsigned cost = std::max(1u, used) * 2u;
        sa1ClockRemainder_ -= cost;
        ++operations_;
        if(sa1Cpu_.stopped()){sa1Running_=false;break;}
    }
    sa1ActiveRom_ = 0;
    sa1ActiveBwram_ = 0;
    sa1ActiveRamDirty_ = 0;
}

u8 EnhancementChip::superFxReadProgram(const std::vector<u8>& rom,const std::vector<u8>& ram,u32 address) const {
    const u8 bank=static_cast<u8>(address>>16);const u16 offset=static_cast<u16>(address);
    if(bank>=0x70u&&bank<=0x71u&&!ram.empty())return ram[mirror((static_cast<std::size_t>(bank-0x70u)<<16)|offset,ram.size())];
    if(rom.empty())return 0xFFu;
    std::size_t mapped;
    if((bank<=0x3Fu)||(bank>=0x80u&&bank<=0xBFu))
        mapped=(static_cast<std::size_t>(bank&0x3Fu)<<15)|(offset&0x7FFFu);
    else
        mapped=(static_cast<std::size_t>(bank&0x1Fu)<<16)|offset;
    return rom[mirror(mapped,rom.size())];
}

void EnhancementChip::stepSuperFx(unsigned clocks,const std::vector<u8>& rom,std::vector<u8>& cartridgeRam,bool& ramDirty){
    enum { Z=0x0002u,CY=0x0004u,S=0x0008u,OV=0x0010u,G=0x0020u,
           ALT1=0x0100u,ALT2=0x0200u,B=0x1000u,IRQ=0x8000u };
    std::vector<u8>& ram=cartridgeRam.empty()?superFxRam_:cartridgeRam;
    superFxClockRemainder_+=clocks;
    const unsigned clockUnit=superFxClockSelect_?1u:2u;
    unsigned guard=0u;
    while((superFxStatus_&G)&&superFxClockRemainder_>=clockUnit&&guard++<200000u){
        // The GSU executes from a one-byte instruction pipe.  A control-flow
        // instruction has already prefetched the following sequential opcode;
        // that opcode executes once, while any operand bytes it needs are read
        // from the new R15/PBR.  Preserve that hardware-visible delay slot.
        const bool applyBranch=superFxBranchPending_;
        const u16 branchTarget=superFxBranchTarget_;
        const bool applyBank=superFxBankPending_;
        const u8 bankTarget=superFxBankTarget_;
        superFxBranchPending_=false;
        superFxBankPending_=false;
        unsigned instructionCycles=1u;
        const u32 pc=(static_cast<u32>(superFxPbr_)<<16)|superFxR_[15];
        u8 op;
        const u16 cacheOffset=static_cast<u16>(superFxR_[15]-superFxCacheBase_);
        if(cacheOffset<512u&&superFxCacheValid_[(cacheOffset>>4)&31u])op=superFxCache_[cacheOffset];
        else op=superFxReadProgram(rom,ram,pc);
        superFxTraceAddress_[superFxTracePosition_]=pc;
        superFxTraceOpcode_[superFxTracePosition_]=op;
        superFxTracePosition_=(superFxTracePosition_+1u)&511u;
        if(superFxTraceCount_<512u)++superFxTraceCount_;
        ++superFxR_[15];
        if(applyBranch)superFxR_[15]=branchTarget;
        if(applyBank)superFxPbr_=bankTarget;
        const unsigned alt=((superFxStatus_&ALT1)?1u:0u)|((superFxStatus_&ALT2)?2u:0u);
        const unsigned source=superFxSourceRegister_&15u,destination=superFxDestinationRegister_&15u;
        bool keepPrefixes=false;
        bool wroteRam=false;

        const auto fetch8=[&]()->u8{
            const u16 offset=superFxR_[15]++;
            // All instruction-stream bytes (including IWT and branch
            // operands), not just opcodes, are fetched through the GSU
            // instruction cache. CPU-injected cache programs may have no
            // corresponding ROM code at these addresses.
            const u16 cacheByte=static_cast<u16>(offset-superFxCacheBase_);
            if(cacheByte<512u&&superFxCacheValid_[(cacheByte>>4)&31u])
                return superFxCache_[cacheByte];
            return superFxReadProgram(rom,ram,(static_cast<u32>(superFxPbr_)<<16)|offset);
        };
        const auto readRam8=[&](u16 a)->u8{return ram.empty()?0xFFu:ram[mirror((static_cast<std::size_t>(superFxRambr_&1u)<<16)|a,ram.size())];};
        const auto writeRam8=[&](u16 a,u8 v){if(!ram.empty()){ram[mirror((static_cast<std::size_t>(superFxRambr_&1u)<<16)|a,ram.size())]=v;ramDirty=true;wroteRam=true;}};
        const auto setSZ=[&](u16 v){if(v==0u)superFxStatus_|=Z;else superFxStatus_&=~Z;if(v&0x8000u)superFxStatus_|=S;else superFxStatus_&=~S;};
        const auto setReg=[&](unsigned n,u16 v){n&=15u;if(n==15u){superFxBranchPending_=true;superFxBranchTarget_=v;}else superFxR_[n]=v;if(n==14u)superFxRomBuffer_=superFxReadProgram(rom,ram,(static_cast<u32>(superFxRombr_)<<16)|v);};
        const auto setLogicResult=[&](u16 v){setReg(destination,v);setSZ(v);};
        const auto addResult=[&](u16 a,u16 b,unsigned carry){const u32 r=static_cast<u32>(a)+b+carry;const u16 v=static_cast<u16>(r);if(r&0x10000u)superFxStatus_|=CY;else superFxStatus_&=~CY;if((~(a^b)&(a^v)&0x8000u)!=0u)superFxStatus_|=OV;else superFxStatus_&=~OV;setReg(destination,v);setSZ(v);};
        const auto subFlags=[&](u16 a,u16 b,unsigned borrow)->u16{const u32 operand=static_cast<u32>(b)+borrow;const u16 v=static_cast<u16>(static_cast<u32>(a)-operand);if(static_cast<u32>(a)>=operand)superFxStatus_|=CY;else superFxStatus_&=~CY;if(((a^b)&(a^v)&0x8000u)!=0u)superFxStatus_|=OV;else superFxStatus_&=~OV;setSZ(v);return v;};
        const auto plotAddress=[&](unsigned x,unsigned y,unsigned bpp)->std::size_t{
            unsigned heightMode=((superFxScreenMode_>>2)&1u)|((superFxScreenMode_>>4)&2u);
            if(superFxPlotOption_&0x10u)heightMode=3u;
            std::size_t tile;
            if(heightMode==3u)tile=static_cast<std::size_t>(y&0x80u)*4u+
                static_cast<std::size_t>(x&0x80u)*2u+
                static_cast<std::size_t>((y>>3)&15u)*16u+((x>>3)&15u);
            else{const unsigned rows=heightMode==0u?16u:(heightMode==1u?20u:24u);
                tile=static_cast<std::size_t>(x>>3)*rows+(y>>3);}
            return (static_cast<std::size_t>(superFxScreenBase_)<<10)+tile*(bpp*8u)+(y&7u)*2u;
        };

        if(op==0x00u){superFxStatus_&=~G;superFxStatus_|=IRQ;superFxPlotOption_=0u;superFxBranchPending_=false;superFxBankPending_=false;}
        else if(op==0x01u){keepPrefixes=false;}
        else if(op==0x02u){const u16 base=static_cast<u16>(superFxR_[15]&0xFFF0u);if(base!=superFxCacheBase_){superFxCacheBase_=base;std::fill(superFxCacheValid_.begin(),superFxCacheValid_.end(),0);}}
        else if(op==0x03u){const u16 a=superFxR_[source];if(a&1u)superFxStatus_|=CY;else superFxStatus_&=~CY;setLogicResult(static_cast<u16>(a>>1));}
        else if(op==0x04u){const u16 a=superFxR_[source];const u16 v=static_cast<u16>((a<<1)|((superFxStatus_&CY)?1u:0u));if(a&0x8000u)superFxStatus_|=CY;else superFxStatus_&=~CY;setLogicResult(v);}
        else if(op>=0x05u&&op<=0x0Fu){const s8 rel=static_cast<s8>(fetch8());bool take=false;switch(op){case 0x05:take=true;break;case 0x06:take=((superFxStatus_&S)!=0)==((superFxStatus_&OV)!=0);break;case 0x07:take=((superFxStatus_&S)!=0)!=((superFxStatus_&OV)!=0);break;case 0x08:take=(superFxStatus_&Z)==0;break;case 0x09:take=(superFxStatus_&Z)!=0;break;case 0x0A:take=(superFxStatus_&S)==0;break;case 0x0B:take=(superFxStatus_&S)!=0;break;case 0x0C:take=(superFxStatus_&CY)==0;break;case 0x0D:take=(superFxStatus_&CY)!=0;break;case 0x0E:take=(superFxStatus_&OV)==0;break;case 0x0F:take=(superFxStatus_&OV)!=0;break;}if(take){superFxBranchPending_=true;superFxBranchTarget_=static_cast<u16>(superFxR_[15]+rel);}keepPrefixes=true;instructionCycles=2u;}
        else if(op>=0x10u&&op<=0x1Fu){const unsigned n=op&15u;if(superFxStatus_&B){setReg(n,superFxR_[source]);superFxStatus_&=~B;}else{superFxDestinationRegister_=static_cast<u8>(n);keepPrefixes=true;}}
        else if(op>=0x20u&&op<=0x2Fu){const unsigned n=op&15u;superFxSourceRegister_=superFxDestinationRegister_=static_cast<u8>(n);superFxStatus_|=B;keepPrefixes=true;}
        else if(op>=0x30u&&op<=0x3Bu){const unsigned n=op&15u;const u16 a=superFxR_[n];superFxLastRamAddress_=a;writeRam8(a,static_cast<u8>(superFxR_[source]));if(alt==0u)writeRam8(static_cast<u16>(a^1u),static_cast<u8>(superFxR_[source]>>8));instructionCycles=alt?2u:3u;}
        else if(op==0x3Cu){--superFxR_[12];setSZ(superFxR_[12]);if(superFxR_[12]!=0u){superFxBranchPending_=true;superFxBranchTarget_=superFxR_[13];}}
        else if(op==0x3Du){superFxStatus_|=ALT1;superFxStatus_&=~B;superFxAlt_=((superFxStatus_&ALT2)?3u:1u);keepPrefixes=true;}
        else if(op==0x3Eu){superFxStatus_|=ALT2;superFxStatus_&=~B;superFxAlt_=((superFxStatus_&ALT1)?3u:2u);keepPrefixes=true;}
        else if(op==0x3Fu){superFxStatus_|=ALT1|ALT2;superFxStatus_&=~B;superFxAlt_=3u;keepPrefixes=true;}
        else if(op>=0x40u&&op<=0x4Bu){const u16 a=superFxR_[op&15u];superFxLastRamAddress_=a;u16 v=readRam8(a);if(alt==0u)v=static_cast<u16>(v|(static_cast<u16>(readRam8(static_cast<u16>(a^1u)))<<8));setReg(destination,v);instructionCycles=alt?2u:3u;}
        else if(op==0x4Cu){unsigned bpp;switch(superFxScreenMode_&3u){case 0:bpp=2u;break;case 1:case 2:bpp=4u;break;default:bpp=8u;break;}const unsigned x=superFxR_[1]&0xFFu,y=superFxR_[2]&0xFFu;unsigned heightMode=((superFxScreenMode_>>2)&1u)|((superFxScreenMode_>>4)&2u);if(superFxPlotOption_&0x10u)heightMode=3u;const unsigned height=heightMode==0u?128u:(heightMode==1u?160u:(heightMode==2u?192u:256u));if(alt==0u)++superFxR_[1];if(heightMode!=3u&&y<height){const std::size_t base=plotAddress(x,y,bpp);if(alt==0u){u8 color=superFxColor_;if((superFxPlotOption_&2u)&&bpp<=4u)color=((x^y)&1u)?static_cast<u8>(color>>4):color;bool transparent;if(bpp<=4u)transparent=(superFxPlotOption_&1u)==0u&&(color&15u)==0u;else if((superFxPlotOption_&0x10u)==0u)transparent=(superFxPlotOption_&1u)==0u&&(color==0u||((superFxPlotOption_&8u)&&(color&15u)==0u));else transparent=(superFxPlotOption_&1u)==0u&&color==0u;if(!transparent){for(unsigned p=0;p<bpp;++p){const std::size_t a=base+(p>>1)*16u+(p&1u);if(ram.empty())break;u8& byte=ram[mirror(a,ram.size())];const u8 mask=static_cast<u8>(0x80u>>(x&7u));if(color&(1u<<p))byte|=mask;else byte&=static_cast<u8>(~mask);}ramDirty=true;wroteRam=true;}}else{u8 color=0;for(unsigned p=0;p<bpp;++p){const std::size_t a=base+(p>>1)*16u+(p&1u);if(!ram.empty()&&(ram[mirror(a,ram.size())]&(0x80u>>(x&7u))))color|=static_cast<u8>(1u<<p);}setReg(destination,color);}}instructionCycles=5u;}
        else if(op==0x4Du){setLogicResult(static_cast<u16>((superFxR_[source]<<8)|(superFxR_[source]>>8)));}
        else if(op==0x4Eu){if(alt==0u){u8 next=static_cast<u8>(superFxR_[source]);if(superFxPlotOption_&4u)next=static_cast<u8>((next&0xF0u)|(next>>4));if(superFxPlotOption_&8u)superFxColor_=static_cast<u8>((superFxColor_&0xF0u)|(next&0x0Fu));else superFxColor_=next;}else superFxPlotOption_=static_cast<u8>(superFxR_[source]&0x1Fu);}
        else if(op==0x4Fu){setLogicResult(static_cast<u16>(~superFxR_[source]));}
        else if(op>=0x50u&&op<=0x5Fu){const u16 b=(alt&2u)?static_cast<u16>(op&15u):superFxR_[op&15u];addResult(superFxR_[source],b,(alt&1u)&&((superFxStatus_&CY)!=0u));}
        else if(op>=0x60u&&op<=0x6Fu){const u16 b=(alt==2u)?static_cast<u16>(op&15u):superFxR_[op&15u];const unsigned borrow=(alt==1u)&&((superFxStatus_&CY)==0u);const u16 v=subFlags(superFxR_[source],b,borrow);if(alt!=3u)setReg(destination,v);}
        else if(op==0x70u){const u16 v=static_cast<u16>((superFxR_[7]&0xFF00u)|(superFxR_[8]>>8));setReg(destination,v);if(v&0x8080u)superFxStatus_|=S;else superFxStatus_&=~S;if(v&0xC0C0u)superFxStatus_|=OV;else superFxStatus_&=~OV;if(v&0xE0E0u)superFxStatus_|=CY;else superFxStatus_&=~CY;if(v&0xF0F0u)superFxStatus_|=Z;else superFxStatus_&=~Z;}
        else if(op>=0x71u&&op<=0x7Fu){const u16 b=(alt&2u)?static_cast<u16>(op&15u):superFxR_[op&15u];setLogicResult((alt&1u)?static_cast<u16>(superFxR_[source]&~b):static_cast<u16>(superFxR_[source]&b));}
        else if(op>=0x80u&&op<=0x8Fu){const u8 b=(alt&2u)?static_cast<u8>(op&15u):static_cast<u8>(superFxR_[op&15u]);const u8 a=static_cast<u8>(superFxR_[source]);const u16 v=(alt&1u)?static_cast<u16>(static_cast<unsigned>(a)*b):static_cast<u16>(static_cast<s16>(static_cast<s8>(a))*static_cast<s16>(static_cast<s8>(b)));setReg(destination,v);setSZ(v);instructionCycles=2u;}
        else if(op==0x90u){writeRam8(superFxLastRamAddress_,static_cast<u8>(superFxR_[source]));writeRam8(static_cast<u16>(superFxLastRamAddress_^1u),static_cast<u8>(superFxR_[source]>>8));instructionCycles=2u;}
        else if(op>=0x91u&&op<=0x94u){superFxR_[11]=static_cast<u16>(superFxR_[15]+(op-0x90u));}
        else if(op==0x95u){setLogicResult(static_cast<u16>(static_cast<s16>(static_cast<s8>(superFxR_[source]))));}
        else if(op==0x96u){const u16 a=superFxR_[source];if(a&1u)superFxStatus_|=CY;else superFxStatus_&=~CY;const s16 signedValue=static_cast<s16>(a);setLogicResult((alt&1u)&&signedValue==-1?0u:static_cast<u16>(signedValue>>1));}
        else if(op==0x97u){const u16 a=superFxR_[source];const u16 v=static_cast<u16>((a>>1)|((superFxStatus_&CY)?0x8000u:0u));if(a&1u)superFxStatus_|=CY;else superFxStatus_&=~CY;setLogicResult(v);}
        else if(op>=0x98u&&op<=0x9Du){const unsigned n=op&15u;superFxBranchPending_=true;if(alt&1u){superFxBranchTarget_=static_cast<u16>(superFxR_[source]-1u);superFxBankPending_=true;superFxBankTarget_=static_cast<u8>(superFxR_[n]&0x7Fu);const u16 base=static_cast<u16>(superFxR_[source]&0xFFF0u);if(base!=superFxCacheBase_){superFxCacheBase_=base;std::fill(superFxCacheValid_.begin(),superFxCacheValid_.end(),0);}}else superFxBranchTarget_=superFxR_[n];}
        else if(op==0x9Eu){const u16 v=static_cast<u16>(superFxR_[source]&0x00FFu);setReg(destination,v);if(v==0u)superFxStatus_|=Z;else superFxStatus_&=~Z;if(v&0x0080u)superFxStatus_|=S;else superFxStatus_&=~S;}
        else if(op==0x9Fu){const s32 product=static_cast<s32>(static_cast<s16>(superFxR_[source]))*static_cast<s16>(superFxR_[6]);const u32 bits=static_cast<u32>(product);// LMULT writes the low product to R4 before the high product
            // to the destination. If the destination is R4, the high
            // result takes precedence (GSU register write ordering).
            if(alt&1u)superFxR_[4]=static_cast<u16>(bits);
            setReg(destination,static_cast<u16>(bits>>16));
            setSZ(superFxR_[destination]);
            if(bits&0x00008000u)superFxStatus_|=CY;else superFxStatus_&=~CY;
            instructionCycles=4u;}
        else if(op>=0xA0u&&op<=0xAFu){const unsigned n=op&15u;if(alt==0u)setReg(n,static_cast<u16>(static_cast<s16>(static_cast<s8>(fetch8()))));else{const u16 a=static_cast<u16>(fetch8()<<1);superFxLastRamAddress_=a;if(alt&1u)setReg(n,static_cast<u16>(readRam8(a)|(static_cast<u16>(readRam8(static_cast<u16>(a+1u)))<<8)));else{writeRam8(a,static_cast<u8>(superFxR_[n]));writeRam8(static_cast<u16>(a+1u),static_cast<u8>(superFxR_[n]>>8));}}instructionCycles=2u;}
        else if(op>=0xB0u&&op<=0xBFu){const unsigned n=op&15u;if(superFxStatus_&B){const u16 v=superFxR_[n];setReg(destination,v);setSZ(v);if(v&0x0080u)superFxStatus_|=OV;else superFxStatus_&=~OV;superFxStatus_&=~B;}else{superFxSourceRegister_=static_cast<u8>(n);keepPrefixes=true;}}
        else if(op==0xC0u){const u16 v=static_cast<u16>(superFxR_[source]>>8);setReg(destination,v);if(v==0u)superFxStatus_|=Z;else superFxStatus_&=~Z;if(v&0x0080u)superFxStatus_|=S;else superFxStatus_&=~S;}
        else if(op>=0xC1u&&op<=0xCFu){const u16 b=(alt&2u)?static_cast<u16>(op&15u):superFxR_[op&15u];setLogicResult((alt&1u)?static_cast<u16>(superFxR_[source]^b):static_cast<u16>(superFxR_[source]|b));}
        else if(op>=0xD0u&&op<=0xDEu){const unsigned n=op&15u;const u16 v=static_cast<u16>(superFxR_[n]+1u);setReg(n,v);setSZ(v);}
        else if(op==0xDFu){if(alt<=1u){u8 next=superFxRomBuffer_;if(superFxPlotOption_&4u)next=static_cast<u8>((next&0xF0u)|(next>>4));if(superFxPlotOption_&8u)superFxColor_=static_cast<u8>((superFxColor_&0xF0u)|(next&15u));else superFxColor_=next;}else if(alt==2u)superFxRambr_=static_cast<u8>(superFxR_[source]&1u);else superFxRombr_=static_cast<u8>(superFxR_[source]&0x7Fu);}
        else if(op>=0xE0u&&op<=0xEEu){const unsigned n=op&15u;const u16 v=static_cast<u16>(superFxR_[n]-1u);setReg(n,v);setSZ(v);}
        else if(op==0xEFu){u16 v=superFxRomBuffer_;if(alt==1u)v=static_cast<u16>((superFxR_[source]&0x00FFu)|(v<<8));else if(alt==2u)v=static_cast<u16>((superFxR_[source]&0xFF00u)|v);else if(alt==3u)v=static_cast<u16>(static_cast<s16>(static_cast<s8>(v)));setReg(destination,v);}
        else if(op>=0xF0u){const unsigned n=op&15u;const u8 low=fetch8();const u8 high=fetch8();const u16 immediate=static_cast<u16>(low|(static_cast<u16>(high)<<8));if(alt==0u)setReg(n,immediate);else{const u16 a=immediate;superFxLastRamAddress_=a;if(alt&1u)setReg(n,static_cast<u16>(readRam8(a)|(static_cast<u16>(readRam8(static_cast<u16>(a^1u)))<<8)));else{writeRam8(a,static_cast<u8>(superFxR_[n]));writeRam8(static_cast<u16>(a^1u),static_cast<u8>(superFxR_[n]>>8));}}instructionCycles=3u;}

        if(!keepPrefixes){superFxStatus_&=static_cast<u16>(~(ALT1|ALT2|B));superFxAlt_=0u;superFxSourceRegister_=superFxDestinationRegister_=0u;}
        const unsigned cost=std::max(1u,instructionCycles)*clockUnit;
        if(cost>=superFxClockRemainder_)superFxClockRemainder_=0u;else superFxClockRemainder_-=cost;
        ++operations_;
        (void)wroteRam;
    }
}

bool EnhancementChip::readSuperFx(u32 address,const std::vector<u8>& rom,std::vector<u8>& cartridgeRam,u8& value){
    const u8 bank=static_cast<u8>(address>>16);const u16 off=static_cast<u16>(address);const bool sys=bank<=0x3Fu||(bank>=0x80u&&bank<=0xBFu);
    if(sys&&off>=0x3000u&&off<=0x32FFu){const unsigned r=off-0x3000u;
        if((superFxStatus_&0x20u)&&r!=0x30u&&r!=0x31u&&r!=0x3Au&&r!=0x3Bu){value=0;return true;}
        if(r<0x20u){const u16 v=superFxR_[r>>1];value=(r&1u)?static_cast<u8>(v>>8):static_cast<u8>(v);}else if(r==0x30u)value=static_cast<u8>(superFxStatus_);else if(r==0x31u){value=static_cast<u8>(superFxStatus_>>8);superFxStatus_&=0x7FFFu;}else if(r==0x33u)value=1u;else if(r==0x34u)value=superFxPbr_;else if(r==0x36u)value=superFxRombr_;else if(r==0x37u)value=superFxConfig_;else if(r==0x38u)value=superFxScreenBase_;else if(r==0x39u)value=superFxClockSelect_;else if(r==0x3Au)value=superFxScreenMode_;else if(r==0x3Bu)value=0u;else if(r==0x3Cu)value=superFxRambr_;else if(r==0x3Eu)value=static_cast<u8>(superFxCacheBase_);else if(r==0x3Fu)value=static_cast<u8>(superFxCacheBase_>>8);else if(r>=0x100u&&r<0x300u)value=superFxCache_[r-0x100u];else value=0;return true;}
    if(sys&&off>=0x6000u&&off<=0x7FFFu){const std::vector<u8>& m=cartridgeRam.empty()?superFxRam_:cartridgeRam;value=m[(off-0x6000u)%m.size()];return true;}
    if(bank>=0x70u&&bank<=0x71u){const std::vector<u8>& m=cartridgeRam.empty()?superFxRam_:cartridgeRam;value=m[address%m.size()];return true;}
    if(bank>=0x78u&&bank<=0x79u){value=superFxRam_[address%superFxRam_.size()];return true;}
    // With GO and RON asserted the GSU owns the ROM bus.  SNES-side reads see
    // the documented repeating fixed words; in particular this supplies the
    // native/emulation exception vectors while GSU code is running.
    const bool romWindow=(sys&&off>=0x8000u)||
        ((bank>=0x40u&&bank<=0x5Fu)||(bank>=0xC0u&&bank<=0xDFu));
    if(romWindow&&(superFxStatus_&0x20u)&&(superFxScreenMode_&0x10u)){
        static const u8 fixed[16]={0x00,0x01,0x00,0x01,0x04,0x01,0x00,0x01,
                                   0x00,0x01,0x08,0x01,0x00,0x01,0x0C,0x01};
        value=fixed[address&15u];return true;
    }
    (void)rom;return false;
}

bool EnhancementChip::writeSuperFx(u32 address,u8 value,std::vector<u8>& cartridgeRam,bool& ramDirty){
    const u8 bank=static_cast<u8>(address>>16);const u16 off=static_cast<u16>(address);const bool sys=bank<=0x3Fu||(bank>=0x80u&&bank<=0xBFu);
    if(sys&&off>=0x3000u&&off<=0x32FFu){const unsigned r=off-0x3000u;
        if((superFxStatus_&0x20u)&&r!=0x30u&&r!=0x31u&&r!=0x3Au)return true;
        if(r<0x20u){if((r&1u)==0u)superFxRegisterLatch_=value;else{superFxR_[r>>1]=static_cast<u16>(superFxRegisterLatch_|(static_cast<u16>(value)<<8));if((r>>1)==15u)superFxStatus_|=0x20u;}}
        else if (r == 0x30u) {const bool wasRunning=(superFxStatus_&0x20u)!=0u;superFxStatus_=static_cast<u16>((superFxStatus_&~0x003Eu)|(value&0x3Eu));if(wasRunning&&(superFxStatus_&0x20u)==0u){superFxCacheBase_=0;std::fill(superFxCacheValid_.begin(),superFxCacheValid_.end(),0);superFxBranchPending_=false;superFxBankPending_=false;}}
        else if (r == 0x31u) superFxStatus_=static_cast<u16>((superFxStatus_&0x00FFu)|(static_cast<u16>(value)<<8));
        else if (r == 0x34u) superFxPbr_ = static_cast<u8>(value&0x7Fu);
        else if (r == 0x36u) superFxRombr_ = static_cast<u8>(value&0x7Fu);
        else if (r == 0x37u) superFxConfig_ = value;
        else if (r == 0x38u) superFxScreenBase_ = value;
        else if (r == 0x39u) superFxClockSelect_ = static_cast<u8>(value & 1u);
        else if (r == 0x3Au) superFxScreenMode_ = static_cast<u8>(value & 0x3Fu);
        else if (r == 0x3Cu) superFxRambr_ = static_cast<u8>(value & 1u);
        else if (r >= 0x100u && r < 0x300u) {
            superFxCache_[r-0x100u]=value;
            // The GSU exposes a cache line only after the S-CPU commits its
            // final byte.  Marking a partially written line executable can
            // make the coprocessor fetch a mixture of old and new code.
            if((r&0x0Fu)==0x0Fu)superFxCacheValid_[(r-0x100u)>>4]=1u;
        }
        return true;
    }
    if(sys&&off>=0x6000u&&off<=0x7FFFu){std::vector<u8>& m=cartridgeRam.empty()?superFxRam_:cartridgeRam;m[(off-0x6000u)%m.size()]=value;ramDirty=true;return true;}
    if(bank>=0x70u&&bank<=0x71u){std::vector<u8>& m=cartridgeRam.empty()?superFxRam_:cartridgeRam;m[address%m.size()]=value;ramDirty=true;return true;}
    if(bank>=0x78u&&bank<=0x79u){superFxRam_[address%superFxRam_.size()]=value;return true;}return false;
}

std::size_t EnhancementChip::sdd1MapAddress(u32 address,std::size_t romSize) const {
    if(romSize==0u)return 0u;
    const u8 bank=static_cast<u8>(address>>16);const u16 off=static_cast<u16>(address);
    if(bank>=0xC0u){const unsigned slot=(bank-0xC0u)>>4;const std::size_t base=static_cast<std::size_t>(sdd1Regs_[4u+(slot&3u)]&7u)<<20;return mirror(base+((static_cast<std::size_t>(bank)&15u)<<16)+off,romSize);}
    return mirror((static_cast<std::size_t>(bank&0x7Fu)<<15)|(off&0x7FFFu),romSize);
}

void EnhancementChip::initializeSdd1Stream(const std::vector<u8>& rom){
    const std::size_t start=sdd1MapAddress(sdd1DmaSource_,rom.size());
    const u8 header=rom.empty()?0u:rom[start];
    sdd1BitplaneMode_=static_cast<u8>((header>>6)&3u);
    sdd1ContextMode_=static_cast<u8>((header>>4)&3u);
    static const u8 planeCount[4]={2,8,4,0};
    sdd1NumPlanes_=planeCount[sdd1BitplaneMode_];
    sdd1Plane_=sdd1YLocation_=0;
    // The high nibble is the model header; compressed codewords begin at bit
    // 3. The hardware's documented five-bit preload includes one sentinel
    // bit that is shifted out before the first codeword decision.
    sdd1InputBitPosition_=static_cast<u64>(start)*8u+4u;
    sdd1OutputBytePosition_=0;
    for(unsigned i=0;i<8u;++i){sdd1History_[i]=0;sdd1RunCount_[i]=0;sdd1RunHasLps_[i]=false;sdd1RunActive_[i]=false;}
    for(unsigned i=0;i<32u;++i){sdd1ContextState_[i]=0;sdd1ContextMps_[i]=0;}
    sdd1StreamInitialized_=true;
}

unsigned EnhancementChip::sdd1ReadBit(const std::vector<u8>& rom){
    if(rom.empty())return 1u;
    const std::size_t byte=mirror(static_cast<std::size_t>(sdd1InputBitPosition_>>3),rom.size());
    const unsigned shift=7u-static_cast<unsigned>(sdd1InputBitPosition_&7u);
    ++sdd1InputBitPosition_;
    return (rom[byte]>>shift)&1u;
}

unsigned EnhancementChip::sdd1GolombBit(unsigned code,bool& endOfRun,const std::vector<u8>& rom){
    code&=7u;
    if(!sdd1RunActive_[code]){
        const unsigned prefix=sdd1ReadBit(rom);
        if(prefix==0u){sdd1RunCount_[code]=1u<<code;sdd1RunHasLps_[code]=false;}
        else{unsigned suffix=0u;for(unsigned i=0;i<code;++i)suffix|=sdd1ReadBit(rom)<<i;sdd1RunCount_[code]=((1u<<code)-1u)-suffix;sdd1RunHasLps_[code]=true;}
        sdd1RunActive_[code]=true;
    }
    if(sdd1RunCount_[code]!=0u){
        --sdd1RunCount_[code];
        endOfRun=sdd1RunCount_[code]==0u&&!sdd1RunHasLps_[code];
        if(endOfRun)sdd1RunActive_[code]=false;
        return 0u;
    }
    endOfRun=true;sdd1RunHasLps_[code]=false;sdd1RunActive_[code]=false;return 1u;
}

unsigned EnhancementChip::sdd1ContextBit(unsigned plane,const std::vector<u8>& rom){
    static const u8 code[33]={0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,5,5,6,6,7,7,0,1,2,3,4,5,6,7};
    static const u8 nextMps[33]={25,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,24,26,27,28,29,30,31,32,24};
    static const u8 nextLps[33]={25,1,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,1,2,4,8,12,16,18,22};
    const u16 h=sdd1History_[plane&7u];
    const unsigned p1=(h>>0)&1u,p2=(h>>1)&1u,p7=(h>>6)&1u,p8=(h>>7)&1u,p9=(h>>8)&1u;
    unsigned context;
    switch(sdd1ContextMode_){
        case 0:context=p1|(p7<<1)|(p8<<2)|(p9<<3)|((plane&1u)<<4);break;
        case 1:context=p1|(p8<<1)|(p9<<2)|((plane&1u)<<3);break;
        case 2:context=p1|(p7<<1)|(p8<<2)|((plane&1u)<<3);break;
        default:context=p1|(p2<<1)|(p8<<2)|(p9<<3)|((plane&1u)<<4);break;
    }
    context&=31u;
    const unsigned state=sdd1ContextState_[context];
    const unsigned mps=sdd1ContextMps_[context];
    bool end=false;const unsigned raw=sdd1GolombBit(code[state],end,rom);
    const unsigned result=raw^mps;
    if(end){if(raw!=0u&&(state==0u||state==1u))sdd1ContextMps_[context]^=1u;sdd1ContextState_[context]=raw?nextLps[state]:nextMps[state];}
    sdd1History_[plane&7u]=static_cast<u16>(((h<<1)|result)&0x01FFu);
    return result;
}

u8 EnhancementChip::sdd1DecompressByte(const std::vector<u8>& rom){
    if(!sdd1StreamInitialized_)initializeSdd1Stream(rom);
    u8 value=0;
    if(sdd1NumPlanes_==0u){
        for(unsigned plane=0;plane<8u;++plane)
            value=static_cast<u8>((value>>1)|(sdd1ContextBit(plane,rom)<<7));
    }else if((sdd1Plane_&1u)==0u){
        for(unsigned bit=0;bit<8u;++bit){
            (void)sdd1ContextBit(sdd1Plane_,rom);
            (void)sdd1ContextBit(sdd1Plane_+1u,rom);
        }
        value=static_cast<u8>(sdd1History_[sdd1Plane_]);
        ++sdd1Plane_;
    }else{
        value=static_cast<u8>(sdd1History_[sdd1Plane_]);
        --sdd1Plane_;
        if(++sdd1YLocation_==8u){
            sdd1YLocation_=0;
            sdd1Plane_=static_cast<u8>((sdd1Plane_+2u)&(sdd1NumPlanes_-1u));
        }
    }
    ++sdd1OutputBytePosition_;
    return value;
}

bool EnhancementChip::readSdd1(u32 address,const std::vector<u8>& rom,u8& value){
    const u8 bank=static_cast<u8>(address>>16);const u16 off=static_cast<u16>(address);const bool sys=bank<=0x3Fu||(bank>=0x80u&&bank<=0xBFu);
    if(sys&&off>=0x4800u&&off<=0x4807u){value=sdd1Regs_[off-0x4800u];return true;}
    if(sdd1DmaActive_&&sdd1DmaRemaining_!=0u){value=sdd1DecompressByte(rom);--sdd1DmaRemaining_;if(sdd1DmaRemaining_==0u){sdd1DmaActive_=false;sdd1Regs_[1]&=static_cast<u8>(~(1u<<sdd1DmaChannel_));}return true;}
    if(bank>=0xC0u&&!rom.empty()){value=rom[sdd1MapAddress(address,rom.size())];return true;}return false;
}
bool EnhancementChip::writeSdd1(u32 address,u8 value){const u8 bank=static_cast<u8>(address>>16);const u16 off=static_cast<u16>(address);const bool sys=bank<=0x3Fu||(bank>=0x80u&&bank<=0xBFu);if(sys&&off>=0x4800u&&off<=0x4807u){sdd1Regs_[off-0x4800u]=value;++operations_;return true;}return false;}

u8 EnhancementChip::spc7110DataByte(const std::vector<u8>& rom, u32 address) const {
    if (rom.empty()) return 0xFFu;
    const std::size_t dataBase = rom.size() > 0x100000u ? 0x100000u : 0u;
    const std::size_t dataSize = rom.size() - dataBase;
    return rom[dataBase + mirror(address, dataSize)];
}

void EnhancementChip::initializeSpc7110Decompressor(const std::vector<u8>& rom) {
    const u32 directory = make24(spc7110Regs_, 1u);
    const u32 entry = directory + static_cast<u32>(spc7110Regs_[4u]) * 4u;
    spc7110DecompMode_ = static_cast<u8>(spc7110DataByte(rom, entry) % 3u);
    spc7110DecompPointer_ = (static_cast<u32>(spc7110DataByte(rom, entry + 1u)) << 16) |
                            (static_cast<u32>(spc7110DataByte(rom, entry + 2u)) << 8) |
                            spc7110DataByte(rom, entry + 3u);
    spc7110DecompBufferIndex_ = 0;
    spc7110DecompOutput_ = 0;
    spc7110DecompLastPixel_ = 0;
    spc7110DecompTop_ = 255u;
    spc7110DecompValue_ = static_cast<u16>(spc7110DataByte(rom, spc7110DecompPointer_++) << 8);
    spc7110DecompInputBits_ = 0;
    for (unsigned i = 0; i < 16u; ++i) {
        spc7110PixelOrder_[i] = static_cast<u8>(i);
        spc7110RealOrder_[i] = static_cast<u8>(i);
        spc7110BitplaneBuffer_[i] = 0;
    }
    for (unsigned i = 0; i < 32u; ++i) {
        spc7110ContextIndex_[i] = 0;
        spc7110ContextInvert_[i] = 0;
    }
    spc7110DecompInitialized_ = true;
    const unsigned skip = static_cast<unsigned>(spc7110Regs_[5u] |
                                                (static_cast<u16>(spc7110Regs_[6u]) << 8));
    for (unsigned i = 0; i < skip; ++i) (void)spc7110DecompressByte(rom);
    spc7110Regs_[0x0Cu] = 0x80u;
    ++operations_;
}

void EnhancementChip::spc7110DecompressionCore(unsigned context, unsigned& decoded,
                                                const std::vector<u8>& rom) {
    static const u8 probability[53] = {
        90,37,17,8,3,1,90,63,44,32,23,17,12,9,7,5,4,3,2,
        90,72,58,46,38,31,25,21,17,14,11,9,8,7,5,4,4,3,2,
        2,88,77,67,59,52,46,41,37,86,79,71,65,60,55
    };
    static const u8 nextLps[53] = {
        1,6,8,10,12,15,7,19,21,22,23,25,26,28,29,31,32,34,35,
        20,39,40,42,44,45,46,25,26,26,27,28,29,30,31,33,33,34,35,
        36,39,47,48,49,50,51,44,45,47,47,48,49,50,51
    };
    static const u8 nextMps[53] = {
        1,2,3,4,5,5,7,8,9,10,11,12,13,14,15,16,17,18,5,
        20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,
        5,40,41,42,43,44,45,46,24,48,49,50,51,52,43
    };
    context &= 31u;
    decoded = (decoded << 1) ^ spc7110ContextInvert_[context];
    const unsigned evolution = spc7110ContextIndex_[context];
    const unsigned p = probability[evolution];
    spc7110DecompTop_ = static_cast<u16>(spc7110DecompTop_ - p);
    const unsigned high = spc7110DecompValue_ >> 8;
    if (high > spc7110DecompTop_) {
        const unsigned adjusted = high - (spc7110DecompTop_ + 1u);
        spc7110DecompValue_ = static_cast<u16>((adjusted << 8) |
                                               (spc7110DecompValue_ & 0xFFu));
        spc7110DecompTop_ = static_cast<u16>(p - 1u);
        if (spc7110DecompTop_ > 79u) spc7110ContextInvert_[context] ^= 1u;
        decoded ^= 1u;
        spc7110ContextIndex_[context] = nextLps[evolution];
    } else if (spc7110DecompTop_ <= 126u) {
        spc7110ContextIndex_[context] = nextMps[evolution];
    }
    while (spc7110DecompTop_ <= 126u) {
        if (spc7110DecompInputBits_ == 0u) {
            spc7110DecompValue_ = static_cast<u16>((spc7110DecompValue_ & 0xFF00u) |
                                                   spc7110DataByte(rom, spc7110DecompPointer_++));
            spc7110DecompInputBits_ = 8u;
        }
        spc7110DecompTop_ = static_cast<u16>((spc7110DecompTop_ << 1) + 1u);
        spc7110DecompValue_ = static_cast<u16>(spc7110DecompValue_ << 1);
        --spc7110DecompInputBits_;
    }
}

void EnhancementChip::spc7110MovePixelToFront(unsigned a, unsigned b, unsigned c,
                                              unsigned shift, unsigned decoded) {
    unsigned x = a;
    for (unsigned m = 0;; ++m) {
        const unsigned old = spc7110PixelOrder_[m];
        spc7110PixelOrder_[m] = static_cast<u8>(x);
        x = old;
        if (x == a) break;
    }
    const unsigned colors = 1u << shift;
    for (unsigned i = 0; i < colors; ++i) spc7110RealOrder_[i] = spc7110PixelOrder_[i];
    const unsigned move[3] = { c, b, a };
    for (unsigned n = 0; n < 3u; ++n) {
        x = move[n];
        for (unsigned m = 0;; ++m) {
            const unsigned old = spc7110RealOrder_[m];
            spc7110RealOrder_[m] = static_cast<u8>(x);
            x = old;
            if (x == move[n]) break;
        }
    }
    spc7110DecompOutput_ = (spc7110DecompOutput_ << shift) +
                           spc7110RealOrder_[decoded & (colors - 1u)];
    spc7110DecompLastPixel_ = static_cast<u8>(b);
}

u8 EnhancementChip::spc7110DecompressByte(const std::vector<u8>& rom) {
    if (!spc7110DecompInitialized_) initializeSpc7110Decompressor(rom);
    const struct Plane {
        static u8 get(u32 out, unsigned shift, unsigned plane) {
            u8 result = 0;
            for (unsigned pixel = 0; pixel < 8u; ++pixel) {
                const unsigned source = (8u * shift - 1u) - pixel * shift - plane;
                result |= static_cast<u8>(((out >> source) & 1u) << (7u - pixel));
            }
            return result;
        }
    } plane;
    u8 result = 0;
    if (spc7110DecompMode_ == 0u) {
        unsigned decoded = 0;
        spc7110DecompressionCore(0u, decoded, rom);
        spc7110DecompressionCore(1u + decoded, decoded, rom);
        spc7110DecompressionCore(3u + decoded, decoded, rom);
        spc7110DecompressionCore(7u + decoded, decoded, rom);
        spc7110DecompOutput_ = (spc7110DecompOutput_ << 4) ^
            (((spc7110DecompOutput_ >> 12) ^ decoded) & 15u);
        decoded = 0;
        spc7110DecompressionCore(15u, decoded, rom);
        spc7110DecompressionCore(16u + decoded, decoded, rom);
        spc7110DecompressionCore(18u + decoded, decoded, rom);
        spc7110DecompressionCore(22u + decoded, decoded, rom);
        spc7110DecompOutput_ = (spc7110DecompOutput_ << 4) ^
            (((spc7110DecompOutput_ >> 12) ^ decoded) & 15u);
        result = static_cast<u8>(spc7110DecompOutput_);
    } else if (spc7110DecompMode_ == 1u) {
        if ((spc7110DecompBufferIndex_ & 1u) == 0u) {
            for (unsigned pixel = 0; pixel < 8u; ++pixel) {
                const unsigned a = (spc7110DecompOutput_ >> 2) & 3u;
                const unsigned b = (spc7110DecompOutput_ >> 14) & 3u;
                unsigned decoded = 0;
                unsigned context = (a == b && b == spc7110DecompLastPixel_) ? 0u :
                    (a == b ? 1u : (b == spc7110DecompLastPixel_ ? 2u :
                    (a == spc7110DecompLastPixel_ ? 3u : 4u)));
                spc7110DecompressionCore(context, decoded, rom);
                context = context * 2u + 5u + decoded;
                spc7110DecompressionCore(context, decoded, rom);
                spc7110MovePixelToFront(a, b, spc7110DecompLastPixel_, 2u, decoded);
            }
            result = plane.get(spc7110DecompOutput_, 2u, 0u);
        } else {
            result = plane.get(spc7110DecompOutput_, 2u, 1u);
        }
    } else {
        static const u8 contextTable[15] = { 0,0,0,15,17,19,21,23,25,25,25,25,25,27,29 };
        if ((spc7110DecompBufferIndex_ & 0x11u) == 0u) {
            for (unsigned pixel = 0; pixel < 8u; ++pixel) {
                const unsigned a = spc7110DecompOutput_ & 15u;
                const unsigned b = (spc7110DecompOutput_ >> 28) & 15u;
                unsigned decoded = 0;
                unsigned context = 0;
                spc7110DecompressionCore(context, decoded, rom);
                context = decoded + 1u;
                spc7110DecompressionCore(context, decoded, rom);
                if (context == 2u) context = decoded + 11u;
                else {
                    const unsigned relation =
                        (a == b && b == spc7110DecompLastPixel_) ? 0u :
                        (a == b ? 1u : (b == spc7110DecompLastPixel_ ? 2u :
                        (a == spc7110DecompLastPixel_ ? 3u : 4u)));
                    context = relation + 3u + decoded * 5u;
                }
                spc7110DecompressionCore(context, decoded, rom);
                context = contextTable[context] + (decoded & 1u);
                spc7110DecompressionCore(context, decoded, rom);
                spc7110MovePixelToFront(a, b, spc7110DecompLastPixel_, 4u, decoded);
            }
            const unsigned index = spc7110DecompBufferIndex_ & 15u;
            spc7110BitplaneBuffer_[index] = plane.get(spc7110DecompOutput_, 4u, 2u);
            spc7110BitplaneBuffer_[index + 1u] = plane.get(spc7110DecompOutput_, 4u, 3u);
            result = plane.get(spc7110DecompOutput_, 4u, 0u);
        } else if ((spc7110DecompBufferIndex_ & 0x10u) == 0u) {
            result = plane.get(spc7110DecompOutput_, 4u, 1u);
        } else {
            result = spc7110BitplaneBuffer_[spc7110DecompBufferIndex_ & 15u];
        }
    }
    ++spc7110DecompBufferIndex_;
    return result;
}

u8 EnhancementChip::spc7110ReadDataPort(const std::vector<u8>& rom, bool offsetPort) {
    if (!spc7110DataPortActive_) return 0u;
    const u8 mode = spc7110Regs_[0x18u];
    const u32 base = make24(spc7110Regs_, 0x11u);
    const u16 rawOffset = static_cast<u16>(spc7110Regs_[0x14u] |
                                           (static_cast<u16>(spc7110Regs_[0x15u]) << 8));
    const s32 offset = (mode & 0x08u) ? static_cast<s16>(rawOffset) : rawOffset;
    const u32 location = static_cast<u32>(base + (offsetPort || (mode & 0x02u) ? offset : 0));
    const u8 result = spc7110DataByte(rom, location);
    if (offsetPort) {
        if (((mode >> 5) & 3u) == 3u) {
            const u32 next = static_cast<u32>(base + offset);
            for (unsigned i = 0; i < 3u; ++i) spc7110Regs_[0x11u + i] = static_cast<u8>(next >> (i * 8u));
            spc7110DataPointer_ = next;
        }
        return result;
    }
    u32 step = 1u;
    if (mode & 1u) {
        const u16 rawStep = static_cast<u16>(spc7110Regs_[0x16u] |
                                             (static_cast<u16>(spc7110Regs_[0x17u]) << 8));
        step = (mode & 0x04u) ? static_cast<u32>(static_cast<s32>(static_cast<s16>(rawStep))) : rawStep;
    }
    if (mode & 0x10u) {
        const u16 next = static_cast<u16>(rawOffset + step);
        spc7110Regs_[0x14u] = static_cast<u8>(next);
        spc7110Regs_[0x15u] = static_cast<u8>(next >> 8);
    } else {
        const u32 next = base + step;
        for (unsigned i = 0; i < 3u; ++i) spc7110Regs_[0x11u + i] = static_cast<u8>(next >> (i * 8u));
        spc7110DataPointer_ = next;
    }
    return result;
}

void EnhancementChip::executeSpc7110Multiply() {
    const u16 left = static_cast<u16>(spc7110Regs_[0x20u] |
                                      (static_cast<u16>(spc7110Regs_[0x21u]) << 8));
    const u16 right = static_cast<u16>(spc7110Regs_[0x24u] |
                                       (static_cast<u16>(spc7110Regs_[0x25u]) << 8));
    spc7110MathResult_ = (spc7110Regs_[0x2Eu] & 1u) ?
        static_cast<u32>(static_cast<s32>(static_cast<s16>(left)) *
                         static_cast<s32>(static_cast<s16>(right))) :
        static_cast<u32>(left) * right;
    spc7110Remainder_ = 0;
    spc7110Regs_[0x2Fu] = 0x80u;
    ++operations_;
}

void EnhancementChip::executeSpc7110Divide() {
    const u32 dividend = make24(spc7110Regs_, 0x20u) |
                         (static_cast<u32>(spc7110Regs_[0x23u]) << 24);
    const u16 divisor = static_cast<u16>(spc7110Regs_[0x26u] |
                                         (static_cast<u16>(spc7110Regs_[0x27u]) << 8));
    if (divisor == 0u) {
        spc7110MathResult_ = 0;
        spc7110Remainder_ = static_cast<u16>(dividend);
    } else if (spc7110Regs_[0x2Eu] & 1u) {
        const s32 left = static_cast<s32>(dividend);
        const s16 right = static_cast<s16>(divisor);
        if (left == std::numeric_limits<s32>::min() && right == -1) {
            spc7110MathResult_ = static_cast<u32>(left);
            spc7110Remainder_ = 0;
        } else {
            spc7110MathResult_ = static_cast<u32>(left / right);
            spc7110Remainder_ = static_cast<u16>(left % right);
        }
    } else {
        spc7110MathResult_ = dividend / divisor;
        spc7110Remainder_ = static_cast<u16>(dividend % divisor);
    }
    spc7110Regs_[0x2Fu] = 0x80u;
    ++operations_;
}

void EnhancementChip::updateSpc7110Rtc() {
    if (kind_ != SPC7110RTC) return;
    const std::time_t now = std::time(0);
    if ((spc7110RtcRegisters_[0x0Du] & 1u) || (spc7110RtcRegisters_[0x0Fu] & 3u)) {
        spc7110RtcLastUpdate_=static_cast<s64>(now);return;
    }
    const struct RtcCalendar {
        static unsigned pair(const u8* r,unsigned index){return (r[index]&15u)+(r[index+1u]&15u)*10u;}
        static void store(u8* r,unsigned index,unsigned value){r[index]=static_cast<u8>(value%10u);r[index+1u]=static_cast<u8>((value/10u)%10u);}
        static unsigned days(unsigned year,unsigned month){static const unsigned table[12]={31,28,31,30,31,30,31,31,30,31,30,31};unsigned result=table[(month-1u)%12u];const unsigned full=year>=90u?1900u+year:2000u+year;if(month==2u&&(full%4u)==0u&&((full%100u)!=0u||(full%400u)==0u))++result;return result;}
    } calendar;
    if(spc7110RtcLastUpdate_==0){spc7110RtcLastUpdate_=static_cast<s64>(now);return;}
    const s64 elapsed=static_cast<s64>(now)-spc7110RtcLastUpdate_;
    if(elapsed<=0)return;
    unsigned month=calendar.pair(spc7110RtcRegisters_,8u);
    unsigned day=calendar.pair(spc7110RtcRegisters_,6u);
    if(month<1u||month>12u||day<1u||day>31u){spc7110RtcLastUpdate_=static_cast<s64>(now);return;}
    unsigned year=calendar.pair(spc7110RtcRegisters_,10u)%100u;
    unsigned weekday=spc7110RtcRegisters_[0x0Cu]%7u;
    u64 total=static_cast<u64>(calendar.pair(spc7110RtcRegisters_,0u))+static_cast<u64>(elapsed);
    const unsigned second=static_cast<unsigned>(total%60u);total/=60u;
    total+=calendar.pair(spc7110RtcRegisters_,2u);const unsigned minute=static_cast<unsigned>(total%60u);total/=60u;
    total+=calendar.pair(spc7110RtcRegisters_,4u);const unsigned hour=static_cast<unsigned>(total%24u);u64 addDays=total/24u;
    while(addDays!=0u){--addDays;++day;weekday=(weekday+1u)%7u;if(day>calendar.days(year,month)){day=1u;if(++month>12u){month=1u;year=(year+1u)%100u;}}}
    calendar.store(spc7110RtcRegisters_,0u,second);calendar.store(spc7110RtcRegisters_,2u,minute);
    calendar.store(spc7110RtcRegisters_,4u,hour);calendar.store(spc7110RtcRegisters_,6u,day);
    calendar.store(spc7110RtcRegisters_,8u,month);calendar.store(spc7110RtcRegisters_,10u,year);
    spc7110RtcRegisters_[0x0Cu]=static_cast<u8>(weekday);
    spc7110RtcLastUpdate_ = now;
}

u8 EnhancementChip::readSpc7110Rtc() {
    if (kind_ != SPC7110RTC || (spc7110RtcState_ != 4u && spc7110RtcState_ != 5u)) return 0x0Fu;
    updateSpc7110Rtc();
    const u8 result = static_cast<u8>(spc7110RtcRegisters_[spc7110RtcIndex_] & 0x0Fu);
    spc7110RtcIndex_ = static_cast<u8>((spc7110RtcIndex_ + 1u) & 15u);
    return result;
}

void EnhancementChip::writeSpc7110Rtc(u8 value) {
    value &= 0x0Fu;
    if (kind_ != SPC7110RTC || spc7110RtcState_ == 0u) return;
    if (spc7110RtcState_ == 1u) {
        if (value == 0x0Cu) spc7110RtcState_ = 2u;
        else if (value == 0x03u) spc7110RtcState_ = 3u;
        return;
    }
    if (spc7110RtcState_ == 2u || spc7110RtcState_ == 3u) {
        spc7110RtcIndex_ = value;
        spc7110RtcState_=spc7110RtcState_==2u?4u:5u;
        return;
    }
    if (spc7110RtcState_ == 4u || spc7110RtcState_ == 5u) {
        const unsigned index=spc7110RtcIndex_;
        if(index==0x0Du){
            if(value&0x02u){const u8 old13=spc7110RtcRegisters_[0x0Du],old15=spc7110RtcRegisters_[0x0Fu];spc7110RtcRegisters_[0x0Du]&=0x0Eu;spc7110RtcRegisters_[0x0Fu]&=0x0Cu;--spc7110RtcLastUpdate_;updateSpc7110Rtc();spc7110RtcRegisters_[0x0Du]=old13;spc7110RtcRegisters_[0x0Fu]=old15;}
            if(value&0x08u){
                updateSpc7110Rtc();
                const unsigned seconds=(spc7110RtcRegisters_[1u]&7u)*10u+(spc7110RtcRegisters_[0u]&15u);
                spc7110RtcRegisters_[0u]=spc7110RtcRegisters_[1u]=0u;
                if(seconds>=30u){const u8 old13=spc7110RtcRegisters_[0x0Du],old15=spc7110RtcRegisters_[0x0Fu];spc7110RtcRegisters_[0x0Du]&=0x0Eu;spc7110RtcRegisters_[0x0Fu]&=0x0Cu;spc7110RtcLastUpdate_-=60;updateSpc7110Rtc();spc7110RtcRegisters_[0x0Du]=old13;spc7110RtcRegisters_[0x0Fu]=old15;}
            }
        }else if(index==0x0Fu){
            const u8 old=spc7110RtcRegisters_[0x0Fu];
            if((value&1u)&&!(old&1u)){
                updateSpc7110Rtc();
                spc7110RtcRegisters_[0u]=spc7110RtcRegisters_[1u]=0u;
            }
            if((value&2u)&&!(old&2u))updateSpc7110Rtc();
        }
        spc7110RtcRegisters_[index] = value;
        spc7110RtcIndex_ = static_cast<u8>((spc7110RtcIndex_ + 1u) & 15u);
        spc7110RtcLastUpdate_ = std::time(0);
    }
}

bool EnhancementChip::readSpc7110(u32 address, const std::vector<u8>& rom,
                                  const std::vector<u8>& cartridgeRam, u8& value) {
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 off = static_cast<u16>(address);
    const bool system = bank <= 0x3Fu || (bank >= 0x80u && bank <= 0xBFu);
    // $4830.7 is the external SRAM chip-enable signal.  Disabled chips return
    // zero on reads as well as ignoring writes.
    if(system&&off>=0x6000u&&off<=0x7FFFu){
        value=(spc7110Regs_[0x30u]&0x80u)&&!cartridgeRam.empty()?cartridgeRam[(off-0x6000u)%cartridgeRam.size()]:0u;
        return true;
    }
    if(kind_==SPC7110&&off>0x483Fu)return false;
    if (!(system && off >= 0x4800u && off <= 0x4842u)) return false;
    const unsigned reg = off - 0x4800u;
    if (reg == 0u) {
        value = spc7110DecompInitialized_ ? spc7110DecompressByte(rom) : 0u;
        const u16 count = static_cast<u16>(spc7110Regs_[9u] |
                                           (static_cast<u16>(spc7110Regs_[10u]) << 8));
        const u16 next = static_cast<u16>(count - 1u);
        spc7110Regs_[9u] = static_cast<u8>(next);
        spc7110Regs_[10u] = static_cast<u8>(next >> 8);
        spc7110DecompLength_ = next;
    } else if (reg == 0x0Cu) {
        value = spc7110Regs_[reg];
        spc7110Regs_[reg] &= 0x7Fu;
    } else if (reg == 0x10u) value = spc7110ReadDataPort(rom, false);
    else if (reg == 0x1Au) value = spc7110ReadDataPort(rom, true);
    else if (reg >= 0x28u && reg <= 0x2Bu)
        value = static_cast<u8>(spc7110MathResult_ >> ((reg - 0x28u) * 8u));
    else if (reg == 0x2Cu) value = static_cast<u8>(spc7110Remainder_);
    else if (reg == 0x2Du) value = static_cast<u8>(spc7110Remainder_ >> 8);
    else if(reg==0x2Fu){value=spc7110Regs_[reg];spc7110Regs_[reg]&=0x7Fu;}
    else if (reg == 0x41u && kind_ == SPC7110RTC) {value=readSpc7110Rtc();spc7110Regs_[0x42u]=0x80u;}
    else if (reg == 0x42u && kind_ == SPC7110RTC) {value=spc7110Regs_[reg];spc7110Regs_[reg]&=0x7Fu;}
    else value = spc7110Regs_[reg];
    return true;
}

bool EnhancementChip::writeSpc7110(u32 address, u8 value, const std::vector<u8>& rom,
                                   std::vector<u8>& cartridgeRam, bool& ramDirty) {
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 off = static_cast<u16>(address);
    const bool system = bank <= 0x3Fu || (bank >= 0x80u && bank <= 0xBFu);
    if(system&&off>=0x6000u&&off<=0x7FFFu){
        if((spc7110Regs_[0x30u]&0x80u)&&!cartridgeRam.empty()){
            const std::size_t mapped=(off-0x6000u)%cartridgeRam.size();
            if(cartridgeRam[mapped]!=value){cartridgeRam[mapped]=value;ramDirty=true;}
        }
        return true;
    }
    if(kind_==SPC7110&&off>0x483Fu)return false;
    if (!(system && off >= 0x4800u && off <= 0x4842u)) return false;
    const unsigned reg = off - 0x4800u;
    if (reg == 0x40u && kind_ == SPC7110RTC) {
        const bool wasSelected=(spc7110Regs_[0x40u]&1u)!=0u;
        spc7110Regs_[0x40u] = value;
        if (!wasSelected&&(value & 1u)) {spc7110RtcState_ = 1u;spc7110RtcIndex_=0u;spc7110Regs_[0x42u]=0x80u;}
        else if (!(value & 1u)) {
            spc7110RtcState_ = 0u;
            spc7110RtcRegisters_[0x0Fu]&=0x0Eu;
        }
        return true;
    }
    if (reg == 0x41u && kind_ == SPC7110RTC) {
        writeSpc7110Rtc(value);
        spc7110Regs_[0x42u]=0x80u;
        return true;
    }
    spc7110Regs_[reg] = value;
    if (reg == 6u) {
        spc7110DecompInitialized_ = false;
        spc7110Regs_[0x0Cu] = 0;
        initializeSpc7110Decompressor(rom);
    }
    if (reg == 9u || reg == 10u)
        spc7110DecompLength_ = static_cast<u16>(spc7110Regs_[9u] |
                                                (static_cast<u16>(spc7110Regs_[10u]) << 8));
    if (reg >= 0x11u && reg <= 0x13u) {
        spc7110DataPointer_ = make24(spc7110Regs_, 0x11u);
        if (reg == 0x13u && value != 0u) spc7110DataPortActive_ = true;
    }
    if (reg == 0x14u) spc7110OffsetWriteMask_ |= 1u;
    if (reg == 0x15u) spc7110OffsetWriteMask_ |= 2u;
    if ((reg == 0x14u || reg == 0x15u) && spc7110OffsetWriteMask_ == 3u) {
        spc7110DataPortActive_ = true;
        const unsigned action = (spc7110Regs_[0x18u] >> 5) & 3u;
        if (action == 1u || action == 2u) {
            const u32 base = make24(spc7110Regs_, 0x11u);
            const u16 raw = static_cast<u16>(spc7110Regs_[0x14u] |
                                             (static_cast<u16>(spc7110Regs_[0x15u]) << 8));
            const s32 add = action == 1u ? static_cast<s8>(spc7110Regs_[0x14u]) :
                                          static_cast<s16>(raw);
            const u32 next = static_cast<u32>(base + add);
            for (unsigned i = 0; i < 3u; ++i) spc7110Regs_[0x11u + i] = static_cast<u8>(next >> (i * 8u));
            spc7110DataPointer_ = next;
        }
        spc7110OffsetWriteMask_ = 0;
    }
    if (reg == 0x25u) executeSpc7110Multiply();
    if (reg == 0x27u) executeSpc7110Divide();
    if (reg == 0x2Eu) {
        for (unsigned i = 0x20u; i <= 0x2Du; ++i) spc7110Regs_[i] = 0;
        spc7110MathResult_ = 0;
        spc7110Remainder_ = 0;
        spc7110Regs_[0x2Fu] = 0;
    }
    return true;
}

bool EnhancementChip::read(u32 address,const std::vector<u8>& rom,std::vector<u8>& cartridgeRam,u8& value){
    switch(kind_){case DSP:if(isDspPort(address)){if(dspOutput_.empty())value=0xFFu;else{value=dspOutput_.front();dspOutput_.pop_front();}return true;}break;case CX4:return readCx4(address,value);case SA1:return readSa1(address,rom,cartridgeRam,value);case SuperFX:return readSuperFx(address,rom,cartridgeRam,value);case SDD1:return readSdd1(address,rom,value);case SPC7110:case SPC7110RTC:return readSpc7110(address,rom,cartridgeRam,value);default:break;}return false;}

bool EnhancementChip::write(u32 address,u8 value,const std::vector<u8>& rom,std::vector<u8>& cartridgeRam,bool& ramDirty){
    switch(kind_){case DSP:if(isDspPort(address)){if(dspExpected_==0u){dspCommand_=value;dspStage_=0u;dspExpected_=dspExpectedBytes(value);dspInput_.clear();if(dspExpected_==0u){executeDspCommand();dspExpected_=0u;}}else{dspInput_.push_back(value);if(dspInput_.size()>=dspExpected_){const bool complete=executeDspCommand();dspInput_.clear();if(complete){dspExpected_=0u;dspStage_=0u;}}}return true;}break;case CX4:return writeCx4(address,value);case SA1:return writeSa1(address,value,rom,cartridgeRam,ramDirty);case SuperFX:return writeSuperFx(address,value,cartridgeRam,ramDirty);case SDD1:return writeSdd1(address,value);case SPC7110:case SPC7110RTC:return writeSpc7110(address,value,rom,cartridgeRam,ramDirty);default:break;}return false;}

void EnhancementChip::beginDma(unsigned channel,u32 sourceAddress,unsigned length){
    if(kind_!=SDD1||channel>=8u)return;
    const u8 mask=static_cast<u8>(1u<<channel);
    if((sdd1Regs_[0]&sdd1Regs_[1]&mask)==0u)return;
    sdd1DmaActive_=true;sdd1StreamInitialized_=false;sdd1DmaChannel_=channel;
    sdd1DmaSource_=sourceAddress;sdd1DmaRemaining_=length?length:65536u;
}

void EnhancementChip::endDma(unsigned channel){
    if(kind_==SDD1&&sdd1DmaActive_&&channel==sdd1DmaChannel_){sdd1DmaActive_=false;sdd1DmaRemaining_=0;sdd1Regs_[1]&=static_cast<u8>(~(1u<<channel));}
}

bool EnhancementChip::mapRomAddress(u32 address,std::size_t romSize,std::size_t& mapped) const {if(romSize==0)return false;const u8 bank=static_cast<u8>(address>>16);const u16 off=static_cast<u16>(address);const bool sys=bank<=0x3Fu||(bank>=0x80u&&bank<=0xBFu);
    // SA-1 Super MMC selection affects both the S-CPU and the embedded CPU.
    // The old path applied it only to SA-1 instruction/data reads, leaving the
    // game's S-CPU loader on an unrelated plain-LoROM page.
    if(kind_==SA1&&((sys&&off>=0x8000u)||bank>=0xC0u)){mapped=sa1MapRom(address,romSize);return true;}
    // GSU cartridges expose the same linear ROM through 32K LoROM and 64K
    // HiROM views.  Generic LoROM decoding aliases both halves of a HiROM bank
    // and is therefore not sufficient for GSU data/code tables.
    if(kind_==SuperFX){
        if(sys&&off>=0x8000u){mapped=mirror((static_cast<std::size_t>(bank&0x3Fu)<<15)|(off&0x7FFFu),romSize);return true;}
        if((bank>=0x40u&&bank<=0x5Fu)||(bank>=0xC0u&&bank<=0xDFu)){mapped=mirror((static_cast<std::size_t>(bank&0x1Fu)<<16)|off,romSize);return true;}
    }
    if(kind_==SDD1&&bank>=0xC0u){const unsigned slot=(bank-0xC0u)>>4;mapped=mirror((static_cast<std::size_t>(sdd1Regs_[4u+(slot&3u)]&7u)<<20)+((static_cast<std::size_t>(bank)&15u)<<16)+off,romSize);return true;}
    if((kind_==SPC7110||kind_==SPC7110RTC)&&bank>=0xD0u){const unsigned slot=(bank-0xD0u)>>4;const u8 page=spc7110Regs_[0x31u+(slot>2u?2u:slot)];mapped=mirror(0x100000u+(static_cast<std::size_t>(page)<<20)+((static_cast<std::size_t>(bank)&15u)<<16)+off,romSize);return true;}
    return false;}

bool EnhancementChip::exportRtcState(std::vector<u8>& data) const{
    if(kind_!=SPC7110RTC)return false;
    data.assign(24u,0);
    for(unsigned i=0;i<16u;++i)data[i]=static_cast<u8>(spc7110RtcRegisters_[i]&15u);
    const u64 stamp=static_cast<u64>(spc7110RtcLastUpdate_);
    for(unsigned i=0;i<8u;++i)data[16u+i]=static_cast<u8>(stamp>>(i*8u));
    return true;
}

bool EnhancementChip::importRtcState(const std::vector<u8>& data){
    if(kind_!=SPC7110RTC||data.size()!=24u)return false;
    for(unsigned i=0;i<16u;++i)spc7110RtcRegisters_[i]=static_cast<u8>(data[i]&15u);
    u64 stamp=0;for(unsigned i=0;i<8u;++i)stamp|=static_cast<u64>(data[16u+i])<<(i*8u);
    spc7110RtcLastUpdate_=static_cast<s64>(stamp);
    updateSpc7110Rtc();
    return true;
}

void EnhancementChip::step(unsigned masterClocks,const std::vector<u8>& rom,std::vector<u8>& cartridgeRam,bool& ramDirty){if(kind_==SuperFX)stepSuperFx(masterClocks,rom,cartridgeRam,ramDirty);else if(kind_==SA1)stepSa1(masterClocks,rom,cartridgeRam,ramDirty);else if(kind_==SPC7110RTC){static const u64 MasterClocksPerSecond=21477272ull;spc7110RtcClockRemainder_+=masterClocks;while(spc7110RtcClockRemainder_>=MasterClocksPerSecond){spc7110RtcClockRemainder_-=MasterClocksPerSecond;--spc7110RtcLastUpdate_;updateSpc7110Rtc();}}}
EnhancementChip::Kind EnhancementChip::kind() const{return kind_;}
bool EnhancementChip::irqPending() const {
    if(kind_==SA1)return (sa1Regs_[0x100u]&sa1Regs_[0x01u]&0xE0u)!=0u;
    if(kind_==SuperFX)return (superFxStatus_&0x8000u)!=0u&&(superFxConfig_&0x80u)==0u;
    return false;
}
const char* EnhancementChip::implementationStatus() const {switch(kind_){case None:return "standard cartridge";case DSP:return "DSP command processor (common DSP-1 math and complete DSP-2 bitmap protocol; DSP-3/4 remain partial)";case CX4:return "CX4 RAM and math/geometry commands";case SA1:return "SA-1 secondary 65C816, mapping, shared RAM, interrupts, normal/CC2 DMA, arithmetic and timers; exact arbitration and CC1 remain experimental";case SuperFX:return "Super FX documented opcode families, pipelined branches/jumps, RAM/cache and pixel paths; exact bus/cache timing remains experimental";case SDD1:return "S-DD1 streaming decompressor, DMA interception and bank mapping; validated with Street Fighter Alpha 2 and Star Ocean boot sequences";case SPC7110:return "SPC7110 three-mode byte-exact decompressor, data ports, mapping and arithmetic; commercial diagnostics pass";case SPC7110RTC:return "SPC7110 three-mode byte-exact decompressor, data ports, mapping, arithmetic and persistent RTC-4513 protocol; Tengai diagnostic passes";default:return "unknown";}}

unsigned EnhancementChip::secondaryCpuTraceCount() const {
    return kind_ == SA1 ? sa1Cpu_.recentInstructionTraceCount() : 0u;
}

const CPU65816::InstructionTrace& EnhancementChip::secondaryCpuTrace(unsigned index) const {
    return sa1Cpu_.recentInstructionTrace(index);
}

unsigned EnhancementChip::superFxTraceCount() const { return kind_==SuperFX?superFxTraceCount_:0u; }
u32 EnhancementChip::superFxTraceAddress(unsigned index) const {
    if(index>=superFxTraceCount_)return 0u;
    const unsigned start=(superFxTracePosition_+512u-superFxTraceCount_)&511u;
    return superFxTraceAddress_[(start+index)&511u];
}
u8 EnhancementChip::superFxTraceOpcode(unsigned index) const {
    if(index>=superFxTraceCount_)return 0u;
    const unsigned start=(superFxTracePosition_+512u-superFxTraceCount_)&511u;
    return superFxTraceOpcode_[(start+index)&511u];
}

u8 EnhancementChip::diagnosticRegister(unsigned index) const {
    if (kind_ == SuperFX) {
        if (index >= 0x300u && index < 0x320u) {
            const unsigned byte = index - 0x300u;
            const u16 value = superFxR_[byte >> 1];
            return (byte & 1u) ? static_cast<u8>(value >> 8) : static_cast<u8>(value);
        }
        if (index == 0x200u) return static_cast<u8>(superFxStatus_);
        if (index == 0x201u) return static_cast<u8>(superFxStatus_ >> 8);
        if (index == 0x202u) return superFxPbr_;
        if (index == 0x203u) return static_cast<u8>(superFxR_[15]);
        if (index == 0x204u) return static_cast<u8>(superFxR_[15] >> 8);
        if (index == 0x205u) return superFxScreenMode_;
        if (index == 0x206u) return superFxRombr_;
        if (index == 0x207u) return superFxRambr_;
    }
    if(kind_==SA1&&index>=0x400u&&index<0xC00u)return sa1Iram_[index-0x400u];
    return index < sa1Regs_.size() ? sa1Regs_[index] : 0u;
}
u64 EnhancementChip::operations() const{return operations_;}

} // namespace snes
