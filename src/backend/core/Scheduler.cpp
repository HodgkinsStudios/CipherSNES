#include "snes/core/Scheduler.hpp"
#include "snes/core/CPU65816.hpp"
#include "snes/core/PPU.hpp"
#include "snes/core/APU.hpp"
#include "snes/core/DMA.hpp"
#include "snes/core/Bus.hpp"

namespace snes {
namespace {
// Expose the NMI edge only after the VBlank H=2 transition has
// actually been clocked. An anticipatory look-ahead at H=0..1 tells
// the CPU that NMI arrived before the hardware asserted the signal.
// The CPU's persistent nmiPending() records the subsequent pending edge.
// CP66: correct the packaged CP65 source to reproduce its tested binary.
bool nmiEdgeWithinFour(const PPU* ppu, const Bus* bus) {
    if (!ppu || !bus || !bus->nmiEnabled()) return false;
    if (ppu->scanline() != ppu->visibleLineCount() + 1) return false;
    const int clock = ppu->masterClock();
    return clock >= 2 && clock < 6;
}
}

Scheduler::Scheduler()
    : cpu_(0), ppu_(0), apu_(0), dma_(0), bus_(0), masterCycles_(0), framesRun_(0),
      ppuRemainder_(0), apuRemainder_(0), processingEvents_(false), irqPolledOnWrite_(false), nmiBusPollSample_(false), cpuDmaWriteDuringInstruction_(false), postDmaInterruptInhibit_(false), postDmaReleaseMasterCycle_(0) {}

void Scheduler::connect(CPU65816* cpu, PPU* ppu, APU* apu, DMA* dma, Bus* bus) {
    cpu_ = cpu;
    ppu_ = ppu;
    apu_ = apu;
    dma_ = dma;
    bus_ = bus;
    if (bus_) bus_->connectScheduler(this);
}

void Scheduler::reset() {
    masterCycles_ = 0;
    framesRun_ = 0;
    ppuRemainder_ = 0;
    apuRemainder_ = 0;
    processingEvents_ = false;
    irqPolledOnWrite_ = false;
    nmiBusPollSample_ = false;
    cpuDmaWriteDuringInstruction_ = false;
    postDmaInterruptInhibit_ = false;
    postDmaReleaseMasterCycle_ = 0;
}

void Scheduler::advanceHardware(unsigned masterClocks) {
    if (masterClocks == 0u) return;
    masterCycles_ += masterClocks;
    if (bus_) bus_->stepMasterCycles(masterClocks);
    const int oldScanline = ppu_ ? ppu_->scanline() : 0;
    const int oldClock = ppu_ ? ppu_->masterClock() : 0;
    const int oldLineLength = ppu_ ? ppu_->lineMasterClocks() : 1364;
    // Resolve the beginning of VBlank clock by clock: the 5A22's internal
    // /NMI output and the RDNMI status transition at H=2. CPU sampling and
    // final-cycle interrupt admission are handled by the separate bus poll.
    // Ordinary scanlines retain the original bulk fast path.
    const bool nearVBlank = ppu_ && (
        (oldScanline == ppu_->visibleLineCount() && oldClock + static_cast<int>(masterClocks) >= oldLineLength) ||
        (oldScanline == ppu_->visibleLineCount() + 1 && oldClock < 6));
    if (nearVBlank) {
        for (unsigned t = 0; t < masterClocks; ++t) {
            const int sc = ppu_->scanline();
            const int mc = ppu_->masterClock();
            const int ll = ppu_->lineMasterClocks();
            ppu_->stepMasterClocks(1u);
            if (bus_) bus_->stepIrqMasterClocks(1u, sc, mc, ll);
            if (ppu_->scanline() == ppu_->visibleLineCount() + 1) {
                // 5A22 /NMI output falls at H=0.5 (two master clocks),
                // not at H=6. The S-CPU samples the latched edge through
                // the usual instruction/bus polling pipeline. A write that
                // disables NMITIMEN later in the same bus slot cannot undo
                // an NMI edge which was already delivered.
                if (ppu_->masterClock() == 2 && bus_) {
                    bus_->notifyVBlankStart();
                    if (cpu_ && bus_->nmiEnabled()) cpu_->requestNmi();
                }
            }
        }
    } else {
        if (ppu_) ppu_->stepMasterClocks(masterClocks);
        if (bus_ && ppu_) bus_->stepIrqMasterClocks(masterClocks, oldScanline, oldClock, oldLineLength);
    }

    // The S-SMP runs from its own oscillator.  Preserve fractional progress
    // against the regional master clock instead of deriving it from the
    // instruction count, which drifted whenever bus speeds or stalls varied.
    if (apu_) {
        const u64 numerator = static_cast<u64>(apuRemainder_) +
                              static_cast<u64>(masterClocks) * 1024000ull;
        // CP52: compile-time constant divisors produce reciprocal-multiply
        // code rather than a costly runtime 64-bit integer divide/modulo at
        // every bus slot. Both NTSC and PAL use exactly the original ratios.
        unsigned apuCycles = 0;
        if (ppu_ && ppu_->isPal()) {
            constexpr u64 palMasterRate = 21281370u;
            apuCycles = static_cast<unsigned>(numerator / palMasterRate);
            apuRemainder_ = static_cast<unsigned>(numerator % palMasterRate);
        } else {
            constexpr u64 ntscMasterRate = 21477272u;
            apuCycles = static_cast<unsigned>(numerator / ntscMasterRate);
            apuRemainder_ = static_cast<unsigned>(numerator % ntscMasterRate);
        }
        if (apuCycles) apu_->stepCycles(apuCycles);
    }
}

void Scheduler::setTimingCallback(TimingCallback callback) {
    timingCallback_ = std::move(callback);
}

void Scheduler::emitTimingEvent(const char* kind, u32 address, u32 value) const {
    if (!timingCallback_) return;
    TimingEvent event{kind, masterCycles_, ppu_ ? ppu_->scanline() : -1,
                      ppu_ ? ppu_->masterClock() : -1,
                      cpu_ ? ((static_cast<u32>(cpu_->pbr()) << 16) | cpu_->pc()) : 0,
                      address, value};
    timingCallback_(event);
}

void Scheduler::advanceBusAccess(unsigned masterClocks) {
    emitTimingEvent("BUS_SLOT_BEGIN", 0, masterClocks);
    // The 65816 interrupt check occurs at the START of its last bus cycle.
    const bool previousNmi = cpu_ && cpu_->nmiPending();
    // Preserve the full bus quantum for APU scheduling and use the S-CPU
    // NMI sampling phase only for the interrupt decision.
    const bool nmiAtPoll = previousNmi || nmiEdgeWithinFour(ppu_, bus_);
    advanceHardware(masterClocks);
    if (cpu_) nmiBusPollSample_ = nmiAtPoll;
    processHardwareEvents();
    emitTimingEvent("BUS_SLOT_END", 0, masterClocks);
}

void Scheduler::processHardwareEvents() {
    if (processingEvents_ || !ppu_) return;
    processingEvents_ = true;
    bool progressed;
    do {
        progressed = false;
        if (ppu_->consumeRefreshStart()) {
            emitTimingEvent("WRAM_REFRESH_BEGIN", 0, 40);
            // WRAM refresh steals forty master clocks from the S-CPU while the
            // PPU, APU, cartridge hardware and delayed MMIO keep advancing.
            advanceHardware(40u);
            if (bus_) bus_->advanceMathCpuCycles(5u);
            progressed = true;
        }
        if (ppu_->consumeHdmaStart()) {
            emitTimingEvent("HDMA_EVENT_BEGIN", 0, dma_ ? dma_->hdmaEnable() : 0);
            if (ppu_->scanline() >= 0 &&
                ppu_->scanline() < ppu_->visibleLineCount() + 1 && dma_)
                dma_->runHdmaScanline();
            emitTimingEvent("HDMA_EVENT_END");
            progressed = true;
        }
        if (ppu_->consumeHBlankStart()) progressed = true;
        if (ppu_->consumeVBlankStart()) {
            // RDNMI and the CPU NMI edge were clocked at H=2/H=6 above.
            progressed = true;
        }
        if (ppu_->consumeFrameStart()) {
            emitTimingEvent("FRAME_START");
            if (bus_) bus_->notifyFrameStart();
            if (dma_) dma_->initializeHdma();
            progressed = true;
        }

        if (bus_) {
            if (cpu_ && cpu_->irqLineAsserted() != bus_->irqPending())
                emitTimingEvent("IRQ_LINE_EDGE", 0, bus_->irqPending() ? 1u : 0u);
            if (cpu_) cpu_->setIrqLine(bus_->irqPending());
            if (cpu_ && bus_->consumeNmiEdge()) cpu_->requestNmi();
        }
    } while (progressed);
    processingEvents_ = false;
}

bool Scheduler::runFrame(unsigned instructionBudget, bool renderVideo) {
    if (!cpu_ || !ppu_) return false;
    ppu_->setFrameRenderingEnabled(renderVideo);
    ppu_->consumeFrameReady();

    for (unsigned instruction = 0; instruction < instructionBudget; ++instruction) {
        if (!stepInstruction()) return false;

        if (ppu_->consumeFrameReady()) {
            ++framesRun_;
            return true;
        }
    }
    return false;
}

bool Scheduler::stepInstruction() {
    if (!cpu_ || !ppu_) return false;
    if (bus_) bus_->beginCpuInstruction();
    irqPolledOnWrite_ = false;
    cpuDmaWriteDuringInstruction_ = false;
    nmiBusPollSample_ = cpu_->nmiPending();
    cpu_->setInstructionNmiPollEnabled(true);
    // The S-CPU H/V IRQ circuit is polled one instruction cycle early.
    // Cartridge coprocessor IRQs remain on the legacy boundary path until
    // their independent assertion clock can be sub-cycle synchronized.
    const bool cyclePoll = bus_ && (bus_->hvIrqEnabled() || cpu_->latchedInstructionIrqPending()) && !bus_->cartridgeIrqPending();
    cpu_->setInstructionIrqPollEnabled(cyclePoll);
    const bool irqMaskedBeforeInstruction = (cpu_->p() & CPU65816::IrqDisable) != 0u;
    const u64 instructionsBefore = cpu_->instructions();
    const unsigned cpuCycles = cpu_->step();
    // Bus slots have already advanced hardware using their actual 6/8/12
    // master-clock speed. Add only the truly internal 65C816 cycles. The
    // previous max(cycles*6, bus clocks) formula lost internal cycles as soon
    // as slow-ROM accesses exceeded the six-clock nominal floor.
    if (bus_) bus_->consumeAccessMasterCycles();
    const unsigned busAccesses = bus_ ? bus_->consumeCpuBusAccesses() : 0u;
    // Poll the interrupt at the last completed bus phase before any
    // trailing internal clocks.  This deliberately does not split the
    // hardware advancement: coprocessor synchronization remains invariant.
    if (cpuCycles > busAccesses) {
        const unsigned idleCycles = cpuCycles - busAccesses;
        if (cyclePoll) {
            if (idleCycles > 1u) advanceHardware((idleCycles - 1u) * 6u);
            processHardwareEvents();
            // The CPU samples NMI at the first clock of its final cycle,
            // unlike the IRQ comparator's separately modeled -2 phase.
            const bool nmiAtPoll = cpu_->nmiPending() || nmiEdgeWithinFour(ppu_, bus_);
            advanceHardware(4u);
            processHardwareEvents();
            cpu_->latchInstructionNmiPoll(nmiAtPoll);
            if (!irqPolledOnWrite_) cpu_->latchInstructionIrqPoll(bus_ && bus_->irqPending());
            advanceHardware(2u);
        } else {
            cpu_->latchInstructionIrqPoll(false);
            if (idleCycles > 1u) advanceHardware((idleCycles - 1u) * 6u);
            processHardwareEvents();
            const bool nmiAtPoll = cpu_->nmiPending() || nmiEdgeWithinFour(ppu_, bus_);
            cpu_->latchInstructionNmiPoll(nmiAtPoll);
            advanceHardware(6u);
        }
        if (bus_) bus_->advanceMathCpuCycles(idleCycles);
    } else {
        cpu_->latchInstructionNmiPoll(nmiBusPollSample_);
        if (!irqPolledOnWrite_) cpu_->latchInstructionIrqPoll(bus_ && bus_->irqPending());
    }
    // CLI updates the I mask in the instruction's *final* CPU cycle.  The
    // IRQ poll was performed before that edge, so it observes the old mask:
    // if interrupts were disabled on entry, CLI cannot immediately admit an
    // IRQ before the next opcode runs. This is independent of DMA timing.
    if (irqMaskedBeforeInstruction && cpu_->instructions() != instructionsBefore &&
        cpu_->lastExecutedOpcode() == 0x58u)
        cpu_->latchInstructionIrqPoll(false);

    // CP48: $420B freezes the S-CPU until DMA releases the bus.
    // Interrupts asserted while the bus is seized are not sampled as if each
    // DMA byte were a CPU instruction's final bus read. Keep the pre-DMA
    // instruction's interrupt polls clear; the following CPU instruction
    // will perform its own poll after the DMA boundary.
    // Once DMA yields the bus, the 5A22 interrupt pipeline needs its
    // post-stall CPU cycles before admitting a new IRQ or pending NMI.
    // Twenty master clocks of post-DMA CPU work corresponds to the
    // documented 24-30-clock response with the interrupt fetch overhead.
    // The 16-bit store into $420B performs an additional CPU write to
    // $420C after DMA, accounting naturally for the shorter instruction
    // allowance observed by the original Sour IRQ/DMA tests.
    if (postDmaInterruptInhibit_ &&
        masterCycles_ - postDmaReleaseMasterCycle_ >= 20u)
        postDmaInterruptInhibit_ = false;
    if (cpuDmaWriteDuringInstruction_ || postDmaInterruptInhibit_) {
        cpu_->latchInstructionNmiPoll(false);
        if (!irqPolledOnWrite_) cpu_->latchInstructionIrqPoll(false);
    }
    processHardwareEvents();
    return true;
}

void Scheduler::noteCpuDmaWrite() {
    emitTimingEvent("DMA_CPU_RELEASE");
    cpuDmaWriteDuringInstruction_ = true;
    postDmaInterruptInhibit_ = true;
    postDmaReleaseMasterCycle_ = masterCycles_;
}

void Scheduler::pollIrqAtCpuRegisterWrite() {
    emitTimingEvent("IRQ_WRITE_POLL", 0, bus_ && bus_->irqPending());
    // Writing NMITIMEN ($4200) can deassert the live comparator line, but
    // the CPU already sampled its IRQ eligibility before the write commits.
    // Keep that one poll result through the instruction boundary.
    if (cpu_ && bus_ && !irqPolledOnWrite_ && bus_->hvIrqEnabled()) {
        cpu_->latchInstructionIrqPoll(bus_->cpuIrqTwoClocksAgo());
        irqPolledOnWrite_ = true;
    }
}

u64 Scheduler::masterCycles() const { return masterCycles_; }
u64 Scheduler::framesRun() const { return framesRun_; }

} // namespace snes
