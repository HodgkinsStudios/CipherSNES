#include "snes/core/Bus.hpp"
#include "snes/core/Memory.hpp"
#include "snes/core/Cartridge.hpp"
#include "snes/core/PPU.hpp"
#include "snes/core/APU.hpp"
#include "snes/core/DMA.hpp"
#include "snes/core/Controller.hpp"
#include "snes/core/Scheduler.hpp"

namespace snes {

Bus::Bus()
    : memory_(0), cartridge_(0), ppu_(0), apu_(0), dma_(0), controller1_(0), controller2_(0),
      controller3_(0), controller4_(0), controller5_(0), scheduler_(0),
      openBus_(0), nmitimen_(0), nmiFlag_(false), nmiEdgePending_(false), irqFlag_(false), pio_(0xFFu), wramAddress_(0),
      multiplicand_(0xFFu), dividend_(0xFFFFu), quotient_(0), productOrRemainder_(0), memsel_(0),
      hTimer_(0x01FFu), vTimer_(0x01FFu), lastIrqScanline_(0), lastIrqDot_(0), irqHCounter_(0), irqVCounter_(0), irqCountdown_(0), irqCompareLevel_(false), irqCpuLine_(false), irqLineOneClockAgo_(false), irqLineTwoClocksAgo_(false), accessMasterCycles_(0),
      cpuBusAccesses_(0),
      autoJoypadBusyMasterCycles_(0), mathBusyMasterCycles_(0), mathOperation_(0),
      mathShift_(0), mathPartialMasterClocks_(0), autoJoypadResult_(), dmaBusOwned_(false),
      multitapPort2_(false), readOverrides_() {}

void Bus::connect(Memory* memory, Cartridge* cartridge, PPU* ppu,
                  APU* apu, DMA* dma, Controller* controller1,
                  Controller* controller2, Controller* controller3,
                  Controller* controller4, Controller* controller5) {
    memory_ = memory;
    cartridge_ = cartridge;
    ppu_ = ppu;
    apu_ = apu;
    dma_ = dma;
    controller1_ = controller1;
    controller2_ = controller2;
    controller3_ = controller3;
    controller4_ = controller4;
    controller5_ = controller5;
    if (ppu_) ppu_->setCounterLatchEnabled((pio_ & 0x80u) != 0u);
    if (dma_) dma_->connectBus(this);
}

void Bus::setMultitapPort2(bool enabled) { multitapPort2_ = enabled; }
bool Bus::multitapPort2() const { return multitapPort2_; }

void Bus::connectScheduler(Scheduler* scheduler) { scheduler_ = scheduler; }

void Bus::reset() {
    openBus_ = 0;
    nmitimen_ = 0;
    nmiFlag_ = false;
    nmiEdgePending_ = false;
    irqFlag_ = false;
    pio_ = 0xFFu;
    if (ppu_) ppu_->setCounterLatchEnabled(true);
    wramAddress_ = 0;
    multiplicand_ = 0xFFu;
    dividend_ = 0xFFFFu;
    quotient_ = 0;
    productOrRemainder_ = 0;
    memsel_ = 0;
    hTimer_ = 0x01FFu;
    vTimer_ = 0x01FFu;
    lastIrqScanline_ = 0;
    lastIrqDot_ = 0;
    irqHCounter_ = 0;
    irqVCounter_ = 0;
    irqCountdown_ = 0;
    irqCompareLevel_ = false;
    irqCpuLine_ = false;
    irqLineOneClockAgo_ = false;
    irqLineTwoClocksAgo_ = false;
    accessMasterCycles_ = 0;
    cpuBusAccesses_ = 0;
    autoJoypadBusyMasterCycles_ = 0;
    mathBusyMasterCycles_ = 0;
    mathOperation_ = 0;
    mathShift_ = 0;
    mathPartialMasterClocks_ = 0;
    for (unsigned i = 0; i < 4; ++i) autoJoypadResult_[i] = 0;
    dmaBusOwned_ = false;
}

bool Bus::isSystemBank(u8 bank) const {
    return bank <= 0x3Fu || (bank >= 0x80u && bank <= 0xBFu);
}

u8 Bus::readCpuRegister(u16 address) {
    switch (address) {
        case 0x4016:
            return static_cast<u8>((openBus_ & 0xFCu) |
                (controller1_ ? controller1_->readSerial() : 0u));
        case 0x4017: {
            Controller* first = controller2_;
            Controller* second = 0;
            if (multitapPort2_) {
                const bool firstPair = (pio_ & 0x80u) != 0;
                first = firstPair ? controller2_ : controller4_;
                second = firstPair ? controller3_ : controller5_;
            }
            const u8 data0 = first ? first->readSerial() : 0u;
            const u8 data1 = second ? second->readSerial() : 0u;
            return static_cast<u8>((openBus_ & 0xE0u) | data0 | (data1 << 1) | 0x1Cu);
        }
        case 0x4210: {
            const u8 value = static_cast<u8>((nmiFlag_ ? 0x80u : 0u) | 0x02u | (openBus_ & 0x70u));
            nmiFlag_ = false;
            return value;
        }
        case 0x4211: {
            const u8 value = static_cast<u8>((irqFlag_ ? 0x80u : 0u) | (openBus_ & 0x7Fu));
            if (irqCountdown_ != 1u) { irqFlag_ = false; irqCpuLine_ = false; }
            return value;
        }
        case 0x4212: {
            // HVBJOY only drives bits 7, 6 and 0; bits 1-5 retain the CPU data
            // bus value rather than reading as hard zero.
            u8 value = static_cast<u8>(openBus_ & 0x3Eu);
            if (ppu_ && ppu_->inVBlank()) value |= 0x80u;
            if (ppu_ && ppu_->inHBlank()) value |= 0x40u;
            if (autoJoypadBusyMasterCycles_ != 0) value |= 0x01u;
            return value;
        }
        case 0x4213: return pio_;
        case 0x4214: return static_cast<u8>(quotient_);
        case 0x4215: return static_cast<u8>(quotient_ >> 8);
        case 0x4216: return static_cast<u8>(productOrRemainder_);
        case 0x4217: return static_cast<u8>(productOrRemainder_ >> 8);
        case 0x4218: return static_cast<u8>(autoJoypadResult_[0]);
        case 0x4219: return static_cast<u8>(autoJoypadResult_[0] >> 8);
        case 0x421A: return static_cast<u8>(autoJoypadResult_[1]);
        case 0x421B: return static_cast<u8>(autoJoypadResult_[1] >> 8);
        case 0x421C: return static_cast<u8>(autoJoypadResult_[2]);
        case 0x421D: return static_cast<u8>(autoJoypadResult_[2] >> 8);
        case 0x421E: return static_cast<u8>(autoJoypadResult_[3]);
        case 0x421F: return static_cast<u8>(autoJoypadResult_[3] >> 8);
        default: return openBus_;
    }
}

void Bus::writeCpuRegister(u16 address, u8 value) {
    switch (address) {
        case 0x4016:
            if (controller1_) controller1_->latch((value & 1u) != 0);
            if (controller2_) controller2_->latch((value & 1u) != 0);
            if (controller3_) controller3_->latch((value & 1u) != 0);
            if (controller4_) controller4_->latch((value & 1u) != 0);
            if (controller5_) controller5_->latch((value & 1u) != 0);
            if ((value & 1u) == 0u) schedulePort2LightGun();
            break;
        case 0x4200: {
            if (scheduler_) scheduler_->pollIrqAtCpuRegisterWrite();
            const bool oldNmiEnable = (nmitimen_ & 0x80u) != 0;
            const u8 oldIrqMode = static_cast<u8>(nmitimen_ & 0x30u);
            nmitimen_ = value;
            // Enabling NMI while RDNMI is already asserted generates an NMI
            // edge on real hardware. This matters to software that updates
            // NMITIMEN during VBlank.
            if (!oldNmiEnable && (nmitimen_ & 0x80u) != 0 && nmiFlag_)
                nmiEdgePending_ = true;
            if ((nmitimen_ & 0x30u) == 0) { irqFlag_ = false; irqCpuLine_ = false; irqCountdown_ = 0; }
            // A vertical-only comparator is level-sensitive.  Switching into
            // V-only mode while VTIME already matches produces a rising IRQ
            // condition even though the beam has not crossed the start of the
            // scanline. Do not re-arm on identical $4200 rewrites: those are
            // not new comparator edges. (SNES CPU H/V timer circuit.)
            if (oldIrqMode != 0x20u && (nmitimen_ & 0x30u) == 0x20u &&
                ppu_ && ppu_->scanline() == static_cast<int>(vTimer_))
                { irqFlag_ = true; irqCpuLine_ = true; }
            evaluateIrqComparator(true);
            break;
        }
        case 0x4201:
            if ((pio_ & 0x80u) != 0u && (value & 0x80u) == 0u && ppu_)
                ppu_->latchCountersFromWrioFallingEdge();
            pio_ = value;
            if (ppu_) ppu_->setCounterLatchEnabled((pio_ & 0x80u) != 0u);
            break;
        case 0x4202: multiplicand_ = value; break;
        case 0x4203:
            // Writing WRMPYB always clears RDMPY, even when another ALU
            // operation is active. On an overlap it DOES NOT restart math.
            productOrRemainder_ = 0;
            if (mathOperation_ != 0u) break;
            quotient_ = static_cast<u16>((static_cast<u16>(value) << 8) | multiplicand_);
            mathShift_ = value;
            mathOperation_ = 1u;
            mathBusyMasterCycles_ = 48u;
            mathPartialMasterClocks_ = 0u;
            break;
        case 0x4204: dividend_ = static_cast<u16>((dividend_ & 0xFF00u) | value); break;
        case 0x4205: dividend_ = static_cast<u16>((dividend_ & 0x00FFu) | (static_cast<u16>(value) << 8)); break;
        case 0x4206:
            // WRDIVB loads RDMPY from the current dividend even if a divide
            // is still underway; an in-flight ALU operation keeps its shift
            // register and step count. This is observable in hardware tests.
            productOrRemainder_ = dividend_;
            if (mathOperation_ != 0u) break;
            mathShift_ = static_cast<u32>(value) << 16;
            mathOperation_ = 2u;
            mathBusyMasterCycles_ = 96u;
            mathPartialMasterClocks_ = 0u;
            break;
        case 0x4207: hTimer_ = static_cast<u16>((hTimer_ & 0x0100u) | value); evaluateIrqComparator(true); break;
        case 0x4208: hTimer_ = static_cast<u16>((hTimer_ & 0x00FFu) | (static_cast<u16>(value & 1u) << 8)); evaluateIrqComparator(true); break;
        case 0x4209: vTimer_ = static_cast<u16>((vTimer_ & 0x0100u) | value); evaluateIrqComparator(true); break;
        case 0x420A: vTimer_ = static_cast<u16>((vTimer_ & 0x00FFu) | (static_cast<u16>(value & 1u) << 8)); evaluateIrqComparator(true); break;
        case 0x420B: if (dma_) { traceTimingEvent("DMA_BEGIN", 0, value); const bool triggeredByCpu = value && scheduler_ && !dmaBusOwned_; dma_->start(value); if (triggeredByCpu) scheduler_->noteCpuDmaWrite(); } break;
        case 0x420C: if (dma_) dma_->setHdmaEnable(value); break;
        case 0x420D: memsel_ = value; break;
        default: break;
    }
}

unsigned Bus::accessMasterCycles(u32 address) const {
    address &= 0xFFFFFFu;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 offset = static_cast<u16>(address);
    if (isSystemBank(bank)) {
        if (offset < 0x2000u) return 8u;
        if (offset < 0x4000u) return 6u;
        if (offset < 0x4200u) return 12u;
        if (offset < 0x6000u) return 6u;
        if (offset < 0x8000u) return 8u;
        return ((memsel_ & 1u) && bank >= 0x80u) ? 6u : 8u;
    }
    if (bank >= 0xC0u && (memsel_ & 1u)) return 6u;
    return 8u;
}

void Bus::recordAccess(u32 address) {
    if (dmaBusOwned_) return;
    ++cpuBusAccesses_;
    clockInternalMasterCycles(accessMasterCycles(address));
    // The arithmetic edge belongs to the caller-specific read/write phase:
    // after sampling a read, or before starting a write.  A bus slot is
    // still exactly one 5A22 arithmetic clock, regardless of access speed.
}

void Bus::traceTimingEvent(const char* kind, u32 address, u32 value) const {
    if(scheduler_) scheduler_->emitTimingEvent(kind,address,value);
}
void Bus::beginDmaBusOwnership() {traceTimingEvent("BUS_OWNED_BY_DMA"); dmaBusOwned_ = true; }
void Bus::endDmaBusOwnership() {traceTimingEvent("BUS_RELEASED_BY_DMA"); dmaBusOwned_ = false; }
void Bus::clockInternalMasterCycles(unsigned cycles) {
    accessMasterCycles_ += cycles;
    if (scheduler_) scheduler_->advanceBusAccess(cycles);
}

void Bus::beginCpuInstruction() { accessMasterCycles_ = 0; cpuBusAccesses_ = 0; }
unsigned Bus::consumeAccessMasterCycles() {
    const unsigned value = accessMasterCycles_;
    accessMasterCycles_ = 0;
    return value;
}
unsigned Bus::consumeCpuBusAccesses() {
    const unsigned value = cpuBusAccesses_;
    cpuBusAccesses_ = 0;
    return value;
}

u8 Bus::read(u32 address) {
    address &= 0xFFFFFFu;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 offset = static_cast<u16>(address);
    // 5A22 RDNMI acknowledge is inhibited during the first six master
    // clocks of VBlank. Capture the bus-cycle starting phase, not its end;
    // recordAccess() advances the clock before the register read is sampled.
    bool rdnmiEarly = isSystemBank(bank) && offset == 0x4210u && ppu_ &&
        ((ppu_->scanline() == ppu_->visibleLineCount() + 1 && ppu_->masterClock() < 6) ||
         (ppu_->scanline() == ppu_->visibleLineCount() &&
          ppu_->masterClock() + static_cast<int>(accessMasterCycles(address)) > ppu_->lineMasterClocks() &&
          ppu_->masterClock() + static_cast<int>(accessMasterCycles(address)) - ppu_->lineMasterClocks() < 6));
    recordAccess(address);
    // The CPU-visible RDNMI acknowledge is sampled after the memory
    // access completes.  Polling at its starting phase incorrectly holds
    // the VBlank bit for a read straddling the H=6 release boundary.
    // Raw bus reads without a connected CPU scheduler retain their direct
    // register-sample semantics (as used by isolated MMIO fixtures).
    if (scheduler_ && ppu_ && isSystemBank(bank) && offset == 0x4210u)
        rdnmiEarly = ppu_->scanline() == ppu_->visibleLineCount() + 1 &&
                     ppu_->masterClock() < 6;
    u8 value = openBus_;

    if (memory_ && (bank == 0x7Eu || bank == 0x7Fu)) {
        value = memory_->readWRAM(address - 0x7E0000u);
    } else if (isSystemBank(bank) && offset < 0x2000u && memory_) {
        value = memory_->readWRAM(offset);
    } else if (isSystemBank(bank) && offset >= 0x2100u && offset <= 0x213Fu && ppu_) {
        value = ppu_->readRegister(offset);
    } else if (isSystemBank(bank) && offset >= 0x2140u && offset <= 0x217Fu && apu_) {
        value = apu_->readCpuPort(offset & 3u);
    } else if (isSystemBank(bank) && offset == 0x2180u && memory_) {
        value = memory_->readWRAM(wramAddress_);
        wramAddress_ = (wramAddress_ + 1u) & 0x1FFFFu;
    } else if (isSystemBank(bank) && ((offset >= 0x4016u && offset <= 0x4017u) ||
               (offset >= 0x4210u && offset <= 0x421Fu))) {
        value = readCpuRegister(offset);
    } else if (isSystemBank(bank) && offset >= 0x4300u && offset <= 0x437Fu && dma_) {
        value = dma_->readRegister(static_cast<u16>(offset - 0x4300u));
    } else if (cartridge_ && cartridge_->loaded() &&
               isSystemBank(bank) && offset == 0x230Eu &&
               cartridge_->enhancement() == Cartridge::EnhancementSA1) {
        // The SA-1's documented VERSION CODE port is not physically driven
        // on measured retail devices. The S-CPU sees its pre-existing MDR
        // (open bus), not a hard-coded chip revision or default register 0.
        value = openBus_;
    } else if (cartridge_ && cartridge_->loaded()) {
        value = cartridge_->read(address);
    }

    for (std::size_t i = 0; i < readOverrides_.size(); ++i) {
        const ReadOverride& overrideValue = readOverrides_[i];
        if (overrideValue.address == address && (!overrideValue.hasCompare || overrideValue.compare == value)) {
            value = overrideValue.value;
            break;
        }
    }
    if (scheduler_ && isSystemBank(bank) &&
        (offset == 0x2137u || offset == 0x213Cu || offset == 0x213Du ||
         offset == 0x4211u || offset == 0x4212u))
        scheduler_->emitTimingEvent("MMIO_READ", address, value);
    // Reads initiated at H=0/2/4 see the flag but cannot acknowledge it;
    // at H>=6, the ordinary read-to-clear rule applies. A previously
    // acknowledged zero must never be resurrected by this hold window.
    if (rdnmiEarly && (value & 0x80u)) nmiFlag_ = true;
    openBus_ = value;
    // A CPU read samples the *previous* ALU value.  The 5A22 steps its
    // arithmetic unit at the end of the bus read, not before sampling it.
    // DMA-owned memory accesses are not ordinary CPU bus edges.
    if (scheduler_ && !dmaBusOwned_) advanceMathCycle();
    return value;
}

void Bus::setReadOverrides(const std::vector<ReadOverride>& overrides) { readOverrides_ = overrides; }
void Bus::clearReadOverrides() { readOverrides_.clear(); }

void Bus::write(u32 address, u8 value) {
    address &= 0xFFFFFFu;
    // Writes consume the ALU edge *before* the bus write takes effect.
    // In particular, a write starting multiplication must not also run
    // the first multiply iteration in that same CPU bus cycle.
    if (scheduler_ && !dmaBusOwned_) advanceMathCycle();
    recordAccess(address);
    openBus_ = value;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 offset = static_cast<u16>(address);
    if (scheduler_ && isSystemBank(bank) &&
        (offset == 0x4200u || offset == 0x4201u ||
         (offset >= 0x4207u && offset <= 0x420Cu)))
        scheduler_->emitTimingEvent("MMIO_WRITE", address, value);

    if (memory_ && (bank == 0x7Eu || bank == 0x7Fu)) {
        memory_->writeWRAM(address - 0x7E0000u, value);
        return;
    }
    if (isSystemBank(bank) && offset < 0x2000u && memory_) {
        memory_->writeWRAM(offset, value);
        return;
    }
    if (isSystemBank(bank) && offset >= 0x2100u && offset <= 0x213Fu && ppu_) {
        ppu_->writeRegister(offset, value);
        return;
    }
    if (isSystemBank(bank) && offset >= 0x2140u && offset <= 0x217Fu && apu_) {
        apu_->writeCpuPort(offset & 3u, value);
        return;
    }
    if (isSystemBank(bank) && offset >= 0x2180u && offset <= 0x2183u) {
        if (offset == 0x2180u && memory_) {
            memory_->writeWRAM(wramAddress_, value);
            wramAddress_ = (wramAddress_ + 1u) & 0x1FFFFu;
        } else if (offset == 0x2181u) wramAddress_ = (wramAddress_ & 0x1FF00u) | value;
        else if (offset == 0x2182u) wramAddress_ = (wramAddress_ & 0x100FFu) | (static_cast<u32>(value) << 8);
        else if (offset == 0x2183u) wramAddress_ = (wramAddress_ & 0x0FFFFu) | (static_cast<u32>(value & 1u) << 16);
        return;
    }
    if (isSystemBank(bank) && ((offset >= 0x4016u && offset <= 0x4017u) ||
        (offset >= 0x4200u && offset <= 0x420Du))) {
        writeCpuRegister(offset, value);
        return;
    }
    if (isSystemBank(bank) && offset >= 0x4300u && offset <= 0x437Fu && dma_) {
        dma_->writeRegister(static_cast<u16>(offset - 0x4300u), value);
        return;
    }
    if (cartridge_ && cartridge_->loaded()) cartridge_->write(address, value);
}

u16 Bus::read16(u32 address) {
    const u8 low = read(address);
    const u8 high = read((address & 0xFF0000u) | static_cast<u16>(address + 1u));
    return static_cast<u16>(low | (static_cast<u16>(high) << 8));
}

void Bus::write16(u32 address, u16 value) {
    write(address, static_cast<u8>(value));
    write((address & 0xFF0000u) | static_cast<u16>(address + 1u), static_cast<u8>(value >> 8));
}

bool Bus::nmiEnabled() const { return (nmitimen_ & 0x80u) != 0; }
bool Bus::consumeNmiEdge() { const bool value = nmiEdgePending_; nmiEdgePending_ = false; return value; }
void Bus::notifyVBlankStart() {
    nmiFlag_ = true;
    if (nmitimen_ & 1u) {
        if (controller1_) { controller1_->latch(true); controller1_->latch(false); }
        if (controller2_) { controller2_->latch(true); controller2_->latch(false); }
        if (controller3_) { controller3_->latch(true); controller3_->latch(false); }
        if (controller4_) { controller4_->latch(true); controller4_->latch(false); }
        if (controller5_) { controller5_->latch(true); controller5_->latch(false); }
        schedulePort2LightGun();
        autoJoypadBusyMasterCycles_ = 4224u;
    }
}

void Bus::schedulePort2LightGun() {
    if (!ppu_ || !controller2_ || multitapPort2_ || (pio_ & 0x80u) == 0u) return;
    int horizontal = 0;
    int vertical = 0;
    if (controller2_->lightGunLatchTarget(horizontal, vertical))
        ppu_->scheduleCounterLatch(horizontal, vertical);
}
void Bus::notifyFrameStart() {
    // RDNMI ($4210 bit 7) is asserted only for the current VBlank.
    // Hardware clears it automatically when VBlank ends, even when software
    // never reads $4210. Keeping it latched across frames creates false NMI
    // edges when games temporarily disable and re-enable NMITIMEN.
    nmiFlag_ = false;
    lastIrqScanline_ = 0;
    lastIrqDot_ = 0;
}
bool Bus::consumeNmiFlag() { const bool value = nmiFlag_; nmiFlag_ = false; return value; }
bool Bus::irqPending() const { return irqCpuLine_ || cartridgeIrqPending(); }
bool Bus::cartridgeIrqPending() const { return cartridge_ && cartridge_->enhancementIrqPending(); }
bool Bus::hvIrqEnabled() const { return (nmitimen_ & 0x30u) != 0u; }
bool Bus::cpuIrqTwoClocksAgo() const { return irqLineTwoClocksAgo_ || cartridgeIrqPending(); }
void Bus::clearIrq() { irqFlag_ = false; irqCpuLine_ = false; }

// The 5A22 timer comparator is clocked every four master clocks at H=2,6,10,...
// This models its independent counter/edge stages without copying any reference
// implementation. It is deliberately separate from the PPU renderer dot counter.
void Bus::evaluateIrqComparator(bool registerWrite) {
    const bool h = (nmitimen_ & 0x10u) != 0u;
    const bool v = (nmitimen_ & 0x20u) != 0u;
    const bool level = (h || v) && (!h || hTimer_ == irqHCounter_) &&
                       (!v || vTimer_ == irqVCounter_);
    if (level && !irqCompareLevel_) {
        irqCountdown_ = (h && ppu_ && ppu_->masterClock() == 6) ? 3u : 2u;
    }
    irqCompareLevel_ = level;
    (void)registerWrite;
}

void Bus::tickIrqCounter(int scanline, int lineClock) {
    if (irqCountdown_ > 0u) {
        --irqCountdown_;
        if (irqCountdown_ == 1u && (nmitimen_ & 0x30u)) irqFlag_ = true;
        if (irqCountdown_ == 0u) irqCpuLine_ = irqFlag_;
    }
    if (lineClock == 2) {
        ++irqHCounter_;
        if (scanline == 0) irqVCounter_ = 0;
    } else if (lineClock == 6) {
        irqHCounter_ = 0;
        if (scanline > 0) ++irqVCounter_;
    } else if (lineClock == 10) {
        irqHCounter_ = 0;
    } else if (lineClock > 10) {
        ++irqHCounter_;
    }
    evaluateIrqComparator();
}

void Bus::stepIrqMasterClocks(unsigned clocks, int scanline, int lineClock, int lineLength) {
    // CP52: Most CPU bus slots lie entirely within the linear portion of a
    // scanline while the H/V comparator is disabled.  In that region each
    // comparator tick increments the *internal* H counter, and the 2-clock
    // IRQ history shifts in an unchanged IRQ level.  Batch that mathematically
    // exact interval; retain the original clock-by-clock path across reset
    // dots, active comparators, delayed IRQs and scanline boundaries.
    if (clocks != 0u && lineClock >= 10 &&
        static_cast<unsigned>(lineClock) + clocks < static_cast<unsigned>(lineLength) &&
        (nmitimen_ & 0x30u) == 0u && irqCountdown_ == 0u && !irqCompareLevel_) {
        const unsigned firstTicks = static_cast<unsigned>((lineClock + 2) / 4);
        const unsigned lastTicks = (static_cast<unsigned>(lineClock) + clocks + 2u) / 4u;
        irqHCounter_ += lastTicks - firstTicks;
        if (clocks == 1u) {
            irqLineTwoClocksAgo_ = irqLineOneClockAgo_;
            irqLineOneClockAgo_ = irqCpuLine_;
        } else {
            irqLineOneClockAgo_ = irqCpuLine_;
            irqLineTwoClocksAgo_ = irqCpuLine_;
        }
        return;
    }
    for (unsigned i = 0; i < clocks; ++i) {
        ++lineClock;
        if (lineClock >= lineLength) {
            lineClock = 0;
            ++scanline;
            if (ppu_ && scanline >= ppu_->totalScanlines()) scanline = 0;
            lineLength = 1364; // all but the short interlace/odd-frame scanline
        }
        if ((lineClock & 3) == 2) tickIrqCounter(scanline, lineClock);
        irqLineTwoClocksAgo_ = irqLineOneClockAgo_;
        irqLineOneClockAgo_ = irqCpuLine_;
    }
}

void Bus::updateIrqPosition(int scanline, int dot, int totalScanlines) {
    const bool hEnabled = (nmitimen_ & 0x10u) != 0;
    const bool vEnabled = (nmitimen_ & 0x20u) != 0;
    if ((!hEnabled && !vEnabled) || totalScanlines <= 0) {
        lastIrqScanline_ = scanline;
        lastIrqDot_ = dot;
        return;
    }

    const int totalDots = totalScanlines * 341;
    int oldPosition = lastIrqScanline_ * 341 + lastIrqDot_;
    int newPosition = scanline * 341 + dot;
    if (oldPosition < 0 || oldPosition >= totalDots) oldPosition = 0;
    if (newPosition < 0 || newPosition >= totalDots) newPosition = 0;

    const struct Crossing {
        static bool test(int oldPos, int newPos, int target, int total) {
            if (target < 0 || target >= total) return false;
            if (newPos >= oldPos) return target > oldPos && target <= newPos;
            return target > oldPos || target <= newPos;
        }
    } crossing;

    bool trigger = false;
    if (hEnabled && vEnabled) {
        if (vTimer_ < static_cast<u16>(totalScanlines) && hTimer_ < 341u)
            trigger = crossing.test(oldPosition, newPosition,
                                    static_cast<int>(vTimer_) * 341 + static_cast<int>(hTimer_), totalDots);
    } else if (vEnabled) {
        if (vTimer_ < static_cast<u16>(totalScanlines))
            trigger = crossing.test(oldPosition, newPosition, static_cast<int>(vTimer_) * 341, totalDots);
    } else {
        if (hTimer_ < 341u) {
            int line = lastIrqScanline_;
            for (int checked = 0; checked < 3; ++checked) {
                if (line >= totalScanlines) line -= totalScanlines;
                const int target = line * 341 + static_cast<int>(hTimer_);
                if (crossing.test(oldPosition, newPosition, target, totalDots)) {
                    trigger = true;
                    break;
                }
                if (line == scanline) break;
                ++line;
            }
        }
    }

    if (trigger) { irqFlag_ = true; irqCpuLine_ = true; }
    lastIrqScanline_ = scanline;
    lastIrqDot_ = dot;
}

void Bus::advanceMathCycle() {
    if (mathOperation_ == 0u || mathBusyMasterCycles_ == 0u) return;
    if (mathOperation_ == 1u) {
        // Ricoh 5A22 bit-serial 8x8 multiplier, with RDMPY and RDDIV
        // externally visible at every intermediate iteration.
        if ((quotient_ & 1u) != 0u)
            productOrRemainder_ = static_cast<u16>(productOrRemainder_ + mathShift_);
        quotient_ = static_cast<u16>(quotient_ >> 1);
        mathShift_ <<= 1;
    } else {
        // Sixteen restoring-division iterations. The 17th-bit shifted
        // divisor is intentional: it keeps the first several comparisons
        // from modifying the remainder prematurely.
        quotient_ = static_cast<u16>(quotient_ << 1);
        mathShift_ >>= 1;
        if (static_cast<u32>(productOrRemainder_) >= mathShift_) {
            productOrRemainder_ = static_cast<u16>(productOrRemainder_ - mathShift_);
            quotient_ = static_cast<u16>(quotient_ | 1u);
        }
    }
    mathBusyMasterCycles_ -= 6u;
    if (mathBusyMasterCycles_ == 0u) mathOperation_ = 0u;
}

void Bus::advanceMathCpuCycles(unsigned cycles) {
    while (cycles-- > 0u && mathOperation_ != 0u) advanceMathCycle();
}

void Bus::stepMasterCycles(unsigned cycles) {
    if (cartridge_ && cartridge_->loaded()) cartridge_->step(cycles);
    if (!scheduler_ && mathOperation_ != 0u) {
        // Direct Bus testing without a CPU scheduler uses the master-clock
        // approximation; real CPU execution advances the ALU per CPU cycle.
        // 5A22 arithmetic runs as a sequenced device. Preserve the partial
        // slot across calls and serialize it in save states.
        const unsigned count = mathPartialMasterClocks_ + cycles;
        unsigned steps = count / 6u;
        mathPartialMasterClocks_ = count % 6u;
        while (steps-- > 0u && mathOperation_ != 0u) advanceMathCycle();
    }
    if (autoJoypadBusyMasterCycles_ != 0) {
        if (cycles >= autoJoypadBusyMasterCycles_) {
            autoJoypadBusyMasterCycles_ = 0;
            autoJoypadResult_[0] = controller1_ ? controller1_->autoJoypadWord() : 0;
            autoJoypadResult_[1] = controller2_ ? controller2_->autoJoypadWord() : 0;
            autoJoypadResult_[2] = 0;
            autoJoypadResult_[3] = multitapPort2_ && controller3_ ? controller3_->autoJoypadWord() : 0;
        } else autoJoypadBusyMasterCycles_ -= cycles;
    }
}

void Bus::beginCartridgeDma(unsigned channel,u32 sourceAddress,unsigned length){
    if(cartridge_&&cartridge_->loaded())cartridge_->beginDma(channel,sourceAddress,length);
}

void Bus::endCartridgeDma(unsigned channel){
    if(cartridge_&&cartridge_->loaded())cartridge_->endDma(channel);
}

u8 Bus::sampleInvalidDmaBus() { openBus_ = 0u; return 0u; }
u8 Bus::openBus() const { return openBus_; }
u8 Bus::nmitimen() const { return nmitimen_; }
u16 Bus::hTimer() const { return hTimer_; }
u16 Bus::vTimer() const { return vTimer_; }

} // namespace snes
