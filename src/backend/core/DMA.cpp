#include "snes/core/DMA.hpp"
#include "snes/core/Bus.hpp"

namespace snes {

DMA::Channel::Channel()
    : control(0xFFu), bAddress(0xFFu), aAddress(0xFFFFu), aBank(0xFFu),
      size(0xFFFFu), indirectBank(0xFFu), tableAddress(0xFFFFu),
      lineCounter(0xFFu), unused(0xFFu), indirectAddress(0),
      hdmaLinesRemaining(0), hdmaActive(false), hdmaRepeat(false),
      hdmaDoTransfer(false) {}

DMA::DMA()
    : bus_(0), channels_(), active_(false), activeChannels_(0),
      hdmaEnable_(0), bytesTransferred_(0) {}

void DMA::connectBus(Bus* bus) { bus_ = bus; }

void DMA::reset() {
    for (unsigned i = 0; i < 8; ++i) channels_[i] = Channel();
    active_ = false;
    activeChannels_ = 0;
    hdmaEnable_ = 0;
    bytesTransferred_ = 0;
}

unsigned DMA::bOffset(u8 mode, unsigned index) {
    static const u8 patterns[8][4] = {
        {0,0,0,0}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1},
        {0,1,2,3}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1}
    };
    return patterns[mode & 7u][index & 3u];
}

unsigned DMA::transferLength(u8 mode) {
    static const u8 lengths[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };
    return lengths[mode & 7u];
}

// The A-bus cannot address its own B-bus or the console's CPU/DMA MMIO.
// These masks explicitly cover the 00-3f/80-bf mirrors, not cartridge
// space in banks 40-7d/c0-ff. See the 5A22 DMA address decoder.
bool DMA::validAAddress(u32 address) {
    address &= 0xFFFFFFu;
    return (address & 0x40FF00u) != 0x002100u && // $21xx
           (address & 0x40FE00u) != 0x004000u && // $4000-$41ff
           (address & 0x40FFE0u) != 0x004200u && // $4200-$421f
           (address & 0x40FF80u) != 0x004300u;   // $4300-$437f
}

bool DMA::wramPortConflict(u32 aAddress, u32 bAddress) {
    if (static_cast<u8>(bAddress) != 0x80u) return false;
    aAddress &= 0xFFFFFFu;
    // $7e/$7f WRAM *and* the $0000-$1fff system-bank WRAM mirrors
    // share the physical RAM path with B-bus $2180 (WMDATA).
    return (aAddress & 0xFE0000u) == 0x7E0000u ||
           (aAddress & 0x40E000u) == 0u;
}

void DMA::transferByte(u32 aAddress, u32 bAddress, bool reverse) {
    const bool aValid = validAAddress(aAddress);
    const bool bValid = !wramPortConflict(aAddress, bAddress);
    if (reverse) {
        const u8 value = bValid ? bus_->read(bAddress) : bus_->sampleInvalidDmaBus();
        if (aValid) bus_->write(aAddress, value);
    } else {
        const u8 value = aValid ? bus_->read(aAddress) : bus_->sampleInvalidDmaBus();
        if (bValid) bus_->write(bAddress, value);
    }
    // Invalid accesses still occupy a full bus transfer slot. The caller
    // accounts for the eight master clocks; the Bus suppresses normal CPU
    // access costs while a DMA/HDMA controller owns the bus.
}

void DMA::transferChannel(unsigned index) {
    if (!bus_ || index >= 8u) return;
    Channel& c = channels_[index];
    unsigned remaining = c.size == 0 ? 65536u : c.size;
    const bool reverse = (c.control & 0x80u) != 0;
    const bool fixed = (c.control & 0x08u) != 0;
    const bool decrement = (c.control & 0x10u) != 0;
    const unsigned mode = c.control & 7u;

    bus_->traceTimingEvent("DMA_CHANNEL_BEGIN", index, remaining);
    if(!reverse)bus_->beginCartridgeDma(index,(static_cast<u32>(c.aBank)<<16)|c.aAddress,remaining);

    for (unsigned i = 0; i < remaining && active_; ++i) {
        const u32 aAddress = (static_cast<u32>(c.aBank) << 16) | c.aAddress;
        const u32 bAddress = 0x002100u |
            static_cast<u32>(static_cast<u8>(c.bAddress + bOffset(static_cast<u8>(mode), i)));
        transferByte(aAddress, bAddress, reverse);
        // A-bus and B-bus activity shares one eight-master-clock DMA slot.
        // The Bus suppresses normal CPU access costs while DMA owns it.
        bus_->clockInternalMasterCycles(8u);
        if (!fixed)
            c.aAddress = static_cast<u16>(decrement ? c.aAddress - 1u : c.aAddress + 1u);
        ++bytesTransferred_;
        // Hardware decrements DAS during the transfer. If HDMA takes over
        // a running DMA, retain the still-untransferred byte count.
        c.size = static_cast<u16>(remaining - i - 1u);
    }
    if(!reverse)bus_->endCartridgeDma(index);
    bus_->traceTimingEvent("DMA_CHANNEL_END", index, c.size);

}

void DMA::start(u8 channels) {
    activeChannels_ = channels;
    active_ = channels != 0;
    if (active_ && bus_) {
        bus_->beginDmaBusOwnership();
        bus_->clockInternalMasterCycles(8u);
    }
    for (unsigned i = 0; i < 8 && active_; ++i) {
        if (channels & (1u << i)) {
            if (bus_) bus_->clockInternalMasterCycles(8u);
            transferChannel(i);
        }
    }
    // Release the original DMA bus lock even when HDMA preempted and
    // cleared active_. Leaving it held suppresses every later CPU bus cost.
    if (channels && bus_) bus_->endDmaBusOwnership();
    activeChannels_ = 0;
    active_ = false;
}

void DMA::setHdmaEnable(u8 channels) {
    const u8 previous = hdmaEnable_;
    hdmaEnable_ = channels;
    for (unsigned i = 0; i < 8; ++i) {
        Channel& c = channels_[i];
        const u8 bit = static_cast<u8>(1u << i);
        if (!(channels & bit)) {
            c.hdmaActive = false;
        } else if (!(previous & bit)) {
            // Mid-frame $420C enable makes a programmed HDMA channel
            // eligible at the next HBlank without waiting for frame init.
            c.hdmaActive = true;
            c.hdmaLinesRemaining = (c.lineCounter & 0x7Fu) ? (c.lineCounter & 0x7Fu) : 128u;
            c.hdmaRepeat = (c.lineCounter & 0x80u) != 0u;
            c.hdmaDoTransfer = c.hdmaRepeat;
        }
    }
}

void DMA::initializeHdma() {
    // The frame-start HDMA init phase supersedes any DMA underway.
    if (active_ && hdmaEnable_ != 0u) active_ = false;
    for (unsigned i = 0; i < 8; ++i) {
        Channel& c = channels_[i];
        const bool enabled = (hdmaEnable_ & (1u << i)) != 0;
        // Disabled channels retain the CPU-programmed $43x8-$43xA values;
        // frame initialization must not erase registers that a program may
        // enable later through $420C.
        if (!enabled) continue;
        c.tableAddress = c.aAddress;
        c.indirectAddress = 0;
        c.hdmaLinesRemaining = 0;
        c.hdmaActive = true;
        c.hdmaRepeat = false;
        c.hdmaDoTransfer = false;
        c.lineCounter = 0;
        // Frame-start HDMA initialization preloads each channel's first
        // descriptor so line 0 may transfer before the end-line reload.
        reloadHdmaLineCounter(c);
    }
}

bool DMA::reloadHdmaLineCounter(Channel& c) {
    if (!bus_ || !c.hdmaActive) return false;
    const u32 table = (static_cast<u32>(c.aBank) << 16) | c.tableAddress;
    const u8 descriptor = bus_->read(table);
    bus_->clockInternalMasterCycles(8u);
    c.tableAddress = static_cast<u16>(c.tableAddress + 1u);
    if (descriptor == 0) {
        c.hdmaActive = false;
        c.hdmaLinesRemaining = 0;
        c.lineCounter = 0;
        return false;
    }

    c.hdmaRepeat = (descriptor & 0x80u) != 0;
    c.hdmaLinesRemaining = static_cast<u16>(descriptor & 0x7Fu);
    if (c.hdmaLinesRemaining == 0) c.hdmaLinesRemaining = 128;
    c.hdmaDoTransfer = true;

    if (c.control & 0x40u) {
        const u32 lowAddress = (static_cast<u32>(c.aBank) << 16) | c.tableAddress;
        const u8 low = bus_->read(lowAddress);
        bus_->clockInternalMasterCycles(8u);
        c.tableAddress = static_cast<u16>(c.tableAddress + 1u);
        const u32 highAddress = (static_cast<u32>(c.aBank) << 16) | c.tableAddress;
        const u8 high = bus_->read(highAddress);
        bus_->clockInternalMasterCycles(8u);
        c.tableAddress = static_cast<u16>(c.tableAddress + 1u);
        c.indirectAddress = static_cast<u16>(low | (static_cast<u16>(high) << 8));
        // During indirect HDMA, DAS ($43x5/$43x6) holds the indirect
        // address and is readable by the S-CPU.
        c.size = c.indirectAddress;
    }
    c.lineCounter = descriptor;
    return true;
}

void DMA::transferHdmaChannel(unsigned index, Channel& c) {
    if (!bus_ || index >= 8u) return;
    const unsigned mode = c.control & 7u;
    const unsigned count = transferLength(static_cast<u8>(mode));
    const bool reverse = (c.control & 0x80u) != 0;
    const bool indirect = (c.control & 0x40u) != 0;

    for (unsigned byte = 0; byte < count; ++byte) {
        u32 aAddress;
        if (indirect) {
            aAddress = (static_cast<u32>(c.indirectBank) << 16) | c.indirectAddress;
            c.indirectAddress = static_cast<u16>(c.indirectAddress + 1u);
            c.size = c.indirectAddress;
        } else {
            aAddress = (static_cast<u32>(c.aBank) << 16) | c.tableAddress;
            c.tableAddress = static_cast<u16>(c.tableAddress + 1u);
        }
        const u32 bAddress = 0x002100u |
            static_cast<u32>(static_cast<u8>(c.bAddress + bOffset(static_cast<u8>(mode), byte)));
        transferByte(aAddress, bAddress, reverse);
        bus_->clockInternalMasterCycles(8u);
        ++bytesTransferred_;
    }
}

void DMA::runHdmaScanline() {
    if (!bus_ || hdmaEnable_ == 0u) return;
    const bool displacedDma = active_;
    bus_->traceTimingEvent("HDMA_DISPLACES_DMA", 0, displacedDma ? 1u : 0u);
    active_ = true;
    activeChannels_ = hdmaEnable_;
    if (!displacedDma) bus_->beginDmaBusOwnership();
    // A DMA-in-progress yields to HDMA without paying the ordinary
    // 18-clock start/stop overhead a second time. The DMA/HDMA overlap
    // uses a six-clock handover per original timing-test documentation.
    bus_->clockInternalMasterCycles(displacedDma ? 6u : 18u);

    // Real S-CPU HDMA performs each channel's data transfer before doing
    // *any* channel's line-counter/indirect reload.  Interleaving the two
    // passes changes active B-bus contention and transfer timing.
    bool reload[8] = {};
    for (unsigned i = 0; i < 8u; ++i) {
        if ((hdmaEnable_ & (1u << i)) == 0u) continue;
        bus_->traceTimingEvent("HDMA_CHANNEL", i, channels_[i].hdmaLinesRemaining);
        Channel& c = channels_[i];
        if (!c.hdmaActive) continue;
        if (c.hdmaLinesRemaining > 0u) {
            if (c.hdmaDoTransfer) transferHdmaChannel(i, c);
            --c.hdmaLinesRemaining;
        }
        reload[i] = c.hdmaLinesRemaining == 0u;
        // The descriptor fetch is the eight-clock overhead for a reload;
        // charge a separate idle eight-clock slot only without a reload.
        if (!reload[i]) bus_->clockInternalMasterCycles(8u);
        if (!reload[i]) {
            const u8 remaining = c.hdmaLinesRemaining == 128u ? 0u :
                                  static_cast<u8>(c.hdmaLinesRemaining);
            c.lineCounter = static_cast<u8>((c.hdmaRepeat ? 0x80u : 0u) | remaining);
            c.hdmaDoTransfer = c.hdmaRepeat;
        }
    }
    for (unsigned i = 0; i < 8u; ++i) {
        if (!reload[i]) continue;
        Channel& c = channels_[i];
        if (!c.hdmaActive) continue;
        reloadHdmaLineCounter(c);
    }
    bus_->traceTimingEvent("HDMA_TRANSFER_END", 0, displacedDma ? 1u : 0u);
    if (!displacedDma) bus_->endDmaBusOwnership();
    activeChannels_ = 0;
    active_ = false;
}

u8 DMA::readRegister(u16 address) const {
    const unsigned channel = (address >> 4) & 7u;
    const unsigned reg = address & 0x0Fu;
    const Channel& c = channels_[channel];
    switch (reg) {
        case 0x0: return c.control;
        case 0x1: return c.bAddress;
        case 0x2: return static_cast<u8>(c.aAddress);
        case 0x3: return static_cast<u8>(c.aAddress >> 8);
        case 0x4: return c.aBank;
        case 0x5: return static_cast<u8>(c.size);
        case 0x6: return static_cast<u8>(c.size >> 8);
        case 0x7: return c.indirectBank;
        case 0x8: return static_cast<u8>(c.tableAddress);
        case 0x9: return static_cast<u8>(c.tableAddress >> 8);
        case 0xA: return c.lineCounter;
        case 0xB: case 0xF: return c.unused;
        default: return 0xFFu;
    }
}

void DMA::writeRegister(u16 address, u8 value) {
    const unsigned channel = (address >> 4) & 7u;
    const unsigned reg = address & 0x0Fu;
    Channel& c = channels_[channel];
    switch (reg) {
        case 0x0: c.control = value; break;
        case 0x1: c.bAddress = value; break;
        case 0x2: c.aAddress = static_cast<u16>((c.aAddress & 0xFF00u) | value); break;
        case 0x3: c.aAddress = static_cast<u16>((c.aAddress & 0x00FFu) | (static_cast<u16>(value) << 8)); break;
        case 0x4: c.aBank = value; break;
        case 0x5: c.size = static_cast<u16>((c.size & 0xFF00u) | value); break;
        case 0x6: c.size = static_cast<u16>((c.size & 0x00FFu) | (static_cast<u16>(value) << 8)); break;
        case 0x7: c.indirectBank = value; break;
        case 0x8: c.tableAddress = static_cast<u16>((c.tableAddress & 0xFF00u) | value); break;
        case 0x9: c.tableAddress = static_cast<u16>((c.tableAddress & 0x00FFu) | (static_cast<u16>(value) << 8)); break;
        case 0xA: c.lineCounter = value; break;
        case 0xB: case 0xF: c.unused = value; break;
        default: break;
    }
}

bool DMA::active() const { return active_; }
u8 DMA::hdmaEnable() const { return hdmaEnable_; }
u64 DMA::bytesTransferred() const { return bytesTransferred_; }

} // namespace snes
