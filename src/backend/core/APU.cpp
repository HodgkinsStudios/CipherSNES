#include "snes/core/APU.hpp"
#include <algorithm>
#include <cstring>

namespace snes {
namespace {

// The 64-byte S-SMP IPL ROM shipped in the SNES audio processor.
static const u8 DefaultIplRom[64] = {
    0xCD,0xEF,0xBD,0xE8,0x00,0xC6,0x1D,0xD0,
    0xFC,0x8F,0xAA,0xF4,0x8F,0xBB,0xF5,0x78,
    0xCC,0xF4,0xD0,0xFB,0x2F,0x19,0xEB,0xF4,
    0xD0,0xFC,0x7E,0xF4,0xD0,0x0B,0xE4,0xF5,
    0xCB,0xF4,0xD7,0x00,0xFC,0xD0,0xF3,0xAB,
    0x01,0x10,0xEF,0x7E,0xF4,0x10,0xEB,0xBA,
    0xF6,0xDA,0x00,0xBA,0xF4,0xC4,0xF4,0xDD,
    0x5D,0xD0,0xDB,0x1F,0x00,0x00,0xC0,0xFF
};

} // namespace

APU::Timer::Timer()
    : enabled(false), target(0), divider(0), output(0x0Fu), baseCounter(0) {}

APU::APU()
    : traceEnabled_(false), traceCapacity_(0), traceWrite_(0), traceCount_(0),
      traceSequence_(0), traceDropped_(0), traceInstructionPc_(0), traceRing_(),
      cycles_(0), executionBudget_(0), dspClockRemainder_(0), slowCycleRemainder_(0),
      spcInstructionActive_(false), spcBusCycles_(0),
      portCpuHalfRemainder_(0), portTimerHalfRemainder_(0), ram_(),
      cpuToApu_(), apuToCpu_(), control_(0), test_(0x0A), dspAddress_(0),
      iplRomEnabled_(true), timers_(), spc700_(), dsp_() {
    spc700_.connect(this);
    dsp_.connectRam(ram_);
    std::copy(DefaultIplRom, DefaultIplRom + 64, iplRom_);
    reset();
}

void APU::reset() {
    cycles_ = 0;
    traceWrite_ = traceCount_ = 0;
    traceSequence_ = traceDropped_ = 0;
    executionBudget_ = 0;
    dspClockRemainder_ = 0;
    slowCycleRemainder_ = 0;
    spcInstructionActive_ = false;
    spcBusCycles_ = 0;
    portCpuHalfRemainder_ = portTimerHalfRemainder_ = 0;
    std::memset(ram_, 0, sizeof(ram_));
    for (unsigned i = 0; i < 4; ++i) {
        cpuToApu_[i] = 0;
        apuToCpu_[i] = 0;
    }
    control_ = 0;
    test_ = 0x0A;
    dspAddress_ = 0;
    iplRomEnabled_ = true;
    for (unsigned i = 0; i < 3; ++i) timers_[i] = Timer();
    spc700_.reset();
    dsp_.reset();
}


// Debug trace is disabled in production, bounded (never grows with gameplay),
// and excluded from save states. Phases expose CURRENT timing limitations:
// idle clocks are aggregated after bus transactions, not interleaved.
void APU::enableBusTrace(bool enable, unsigned capacity) {
    traceEnabled_ = enable && capacity != 0;
    traceCapacity_ = traceEnabled_ ? std::min(capacity, 100000u) : 0;
    traceRing_.clear();
    if (traceEnabled_) traceRing_.resize(traceCapacity_);
    traceWrite_ = traceCount_ = 0;
    traceSequence_ = traceDropped_ = 0;
}

std::vector<APU::BusEvent> APU::busTrace() const {
    std::vector<BusEvent> out;
    out.reserve(traceCount_);
    if (!traceCapacity_) return out;
    const unsigned start = (traceWrite_ + traceCapacity_ - traceCount_) % traceCapacity_;
    for (unsigned i = 0; i < traceCount_; ++i)
        out.push_back(traceRing_[(start + i) % traceCapacity_]);
    return out;
}

u64 APU::busTraceDropped() const { return traceDropped_; }

APU::BusDomain APU::domainOf(u16 address) const {
    if (iplRomEnabled_ && address >= 0xFFC0u) return BusDomain::IplRom;
    if (address >= 0x00F0u && address <= 0x00FFu) return BusDomain::Io;
    // All ordinary RAM addresses, including direct page $0000-$00EF, use
    // the external ARAM wait-state domain. Internal applies to idle cycles.
    return BusDomain::Aram;
}

void APU::appendTrace(const BusEvent& event) {
    if (!traceEnabled_) return;
    traceRing_[traceWrite_] = event;
    traceWrite_ = (traceWrite_ + 1) % traceCapacity_;
    if (traceCount_ < traceCapacity_) ++traceCount_;
    else ++traceDropped_;
}

void APU::recordBusAccess(BusKind kind, u16 address, u8 value, u64 before) {
    if (!traceEnabled_) return;
    appendTrace({traceSequence_++, before, cycles_, traceInstructionPc_, address,
                 value, kind, domainOf(address), test_});
}

void APU::retagLastOpcodeFetch(u16 address) {
    if (!traceEnabled_ || !traceCount_) return;
    BusEvent& latest = traceRing_[(traceWrite_ + traceCapacity_ - 1) % traceCapacity_];
    if (latest.address == address && latest.kind == BusKind::Operand)
        latest.kind = BusKind::Opcode;
}

void APU::recordIdle(unsigned cycles, u64 before) {
    if (!traceEnabled_) return;
    appendTrace({traceSequence_++, before, cycles_, traceInstructionPc_, 0xFFFFu,
                 static_cast<u8>(cycles > 255 ? 255 : cycles), BusKind::Idle,
                 BusDomain::Internal, test_});
}

void APU::step() { stepCycles(1); }

void APU::stepCycles(unsigned cycles) {
    executionBudget_ += static_cast<s32>(cycles);
    while (executionBudget_ > 0) {
        // Process memory transactions at their individual S-SMP bus edges.
        // Timer reads/clears and CONTROL/TARGET writes must no longer all
        // occur before one lumped post-instruction hardware advancement.
        spcInstructionActive_ = true;
        if (traceEnabled_) traceInstructionPc_ = spc700_.pc();
        spcBusCycles_ = 0;
        const unsigned used = spc700_.step();
        spcInstructionActive_ = false;
        const unsigned safeUsed = used ? used : 1u;
        // An instruction's remaining internal/idle clocks are charged
        // after its bus transactions. See CP20 timing limitations.
        const unsigned remaining = safeUsed > spcBusCycles_ ? safeUsed - spcBusCycles_ : 0u;
        const u64 idleBefore = cycles_;
        advanceSpcCpuCycles(remaining, BusDomain::Internal);
        if (traceEnabled_ && remaining) recordIdle(remaining, idleBefore);
        if (spc700_.stopped()) {
            // Timers and DSP continue to run even when the SPC700 is asleep.
            if (executionBudget_ > 0) {
                const unsigned idle = static_cast<unsigned>(executionBudget_);
                const u64 idleBefore = cycles_;
                advanceHardware(idle);
                if (traceEnabled_) recordIdle(idle, idleBefore);
                executionBudget_ = 0;
            }
            break;
        }
    }
}

// CP32: independent internal/external S-SMP TEST wait domains. The emulator's
// ordinary APU clock quantum represents the default 2-input-clock wait.
// The two long CPU waits are irregular (10,20 input clocks), while timers
// receive the nominal (8,16). This is source-backed by the ares timing model;
// this is independently authored CipherSNES code, not a source transplant.
unsigned APU::advanceSpcCpuCycles(unsigned count, BusDomain domain) {
    static constexpr unsigned CpuQuanta[4] = {1u, 2u, 5u, 10u};
    static constexpr unsigned TimerQuanta[4] = {1u, 2u, 4u, 8u};
    const unsigned selector = domain == BusDomain::Aram
        ? ((test_ >> 4) & 3u) : ((test_ >> 6) & 3u);
    const unsigned cpuElapsed = count * CpuQuanta[selector];
    const unsigned timerElapsed = count * TimerQuanta[selector];
    advanceHardware(cpuElapsed, timerElapsed);
    executionBudget_ -= static_cast<s32>(cpuElapsed);
    return cpuElapsed;
}

void APU::clockSpcBusAccess(u16 address) {
    if (!spcInstructionActive_) return;
    ++spcBusCycles_;
    advanceSpcCpuCycles(1u, domainOf(address));
}

// For $F4-$F7 the S-SMP samples the CPU->APU port between two waits.
// A hardware wait is two input clocks; our original timing quantum covers
// both. Keep fractional halves only inside the two-part bus transaction,
// advancing the CPU and timer clock domains independently. Never change the
// instruction's total charged wait or truncate the slow TEST wait cases.
void APU::clockSpcInputPortHalf(u16 address) {
    if (!spcInstructionActive_) return;
    const unsigned selector = (test_ >> 6) & 3u; // I/O = internal domain
    static constexpr unsigned CpuQuanta[4] = {1u, 2u, 5u, 10u};
    static constexpr unsigned TimerQuanta[4] = {1u, 2u, 4u, 8u};
    const unsigned accumulatedCpu = portCpuHalfRemainder_ + CpuQuanta[selector];
    const unsigned accumulatedTimer = portTimerHalfRemainder_ + TimerQuanta[selector];
    portCpuHalfRemainder_ = accumulatedCpu & 1u;
    portTimerHalfRemainder_ = accumulatedTimer & 1u;
    const unsigned cpuElapsed = accumulatedCpu / 2u;
    const unsigned timerElapsed = accumulatedTimer / 2u;
    // One complete bus access has two halves. The S-SMP bus phase counter
    // must represent one access, not two independent instructions.
    advanceHardware(cpuElapsed, timerElapsed);
    executionBudget_ -= static_cast<s32>(cpuElapsed);
}

void APU::advanceHardware(unsigned cycles, unsigned timerCycles) {
    cycles_ += cycles;
    advanceTimer(timers_[0], 128u, timerCycles);
    advanceTimer(timers_[1], 128u, timerCycles);
    advanceTimer(timers_[2], 16u, timerCycles);

    const unsigned total = dspClockRemainder_ + cycles;
    const unsigned samples = total / 32u;
    dspClockRemainder_ = total % 32u;
    for (unsigned i = 0; i < samples; ++i) dsp_.stepSample();
}

// The S-SMP timer has a free-running square-wave stage before its gated
// edge detector. Keep baseCounter in *full periods* for binary save-state
// compatibility; the stage-1 level is determined by the half-period phase.
void APU::advanceTimer(Timer& timer, unsigned baseDivider, unsigned cycles) {
    const unsigned half = baseDivider / 2u;
    const unsigned initialPhase = timer.baseCounter % baseDivider;
    const unsigned totalPhase = initialPhase + cycles;
    const unsigned firstHalf = initialPhase / half;
    const unsigned transitions = totalPhase / half - firstHalf;
    timer.baseCounter = totalPhase % baseDivider;
    if (!timer.enabled || (test_ & 0x09u) != 0x08u) return;
    for (unsigned transition = 0; transition < transitions; ++transition) {
        // Only the high-to-low edge increments stage 2.
        if (((firstHalf + transition) & 1u) == 0u) continue;
        timer.divider = static_cast<u8>(timer.divider + 1u);
        if (timer.divider == timer.target) {
            timer.divider = 0;
            timer.output = static_cast<u8>((timer.output + 1u) & 0x0Fu);
        }
    }
}

bool APU::loadIpl(const std::vector<u8>& data) {
    if (data.size() != 64u) return false;
    std::copy(data.begin(), data.end(), iplRom_);
    return true;
}

u8 APU::readCpuPort(unsigned port) const { return apuToCpu_[port & 3u]; }

void APU::writeCpuPort(unsigned port, u8 value) {
    cpuToApu_[port & 3u] = value;
}

u8 APU::readSpcMemory(u16 address) {
    if (address >= 0xFFC0u && iplRomEnabled_)
        return iplRom_[address - 0xFFC0u];

    switch (address) {
        // TEST and CONTROL are write-only and read as zero.
        case 0x00F0: case 0x00F1: return 0;
        case 0x00F2: return dspAddress_;
        case 0x00F3: return dsp_.readRegister(static_cast<u8>(dspAddress_ & 0x7Fu));
        case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F7:
            return cpuToApu_[address - 0x00F4u];
        case 0x00FA: case 0x00FB: case 0x00FC:
            // TnTARGET registers are write-only and read back as zero.
            return 0;
        case 0x00FD: case 0x00FE: case 0x00FF: {
            Timer& timer = timers_[address - 0x00FDu];
            const u8 value = timer.output;
            timer.output = 0;
            return value;
        }
        default: return ram_[address];
    }
}

void APU::writeSpcMemory(u16 address, u8 value) {
    // Writes beneath the enabled IPL ROM still reach RAM.
    // TEST bit 1 gates writes to ordinary ARAM. The SPC I/O register
    // window remains writable when RAM is write-protected.
    if ((test_ & 0x02u) || (address >= 0x00F0u && address <= 0x00FFu))
        ram_[address] = value;
    switch (address) {
        case 0x00F0:
            // Writes are ignored while the SPC700 is using direct page 1.
            if ((spc700_.psw() & SPC700::DirectPage) == 0u) {
                const bool wasEnabled = (test_ & 0x09u) == 0x08u;
                test_ = value;
                const bool isEnabled = (test_ & 0x09u) == 0x08u;
                if (wasEnabled && !isEnabled) {
                    // Disabling the gate while stage 1 is high produces
                    // a falling edge; it can clock stage 2 once.
                    for (unsigned i = 0; i < 3; ++i) {
                        Timer& t = timers_[i];
                        const unsigned half = (i == 2u ? 16u : 128u) / 2u;
                        if (!t.enabled || t.baseCounter < half) continue;
                        t.divider = static_cast<u8>(t.divider + 1u);
                        if (t.divider == t.target) {
                            t.divider = 0;
                            t.output = static_cast<u8>((t.output + 1u) & 0x0Fu);
                        }
                    }
                }
            }
            break;
        case 0x00F1:
            writeControl(value);
            break;
        case 0x00F2:
            dspAddress_ = value;
            break;
        case 0x00F3:
            if (dspAddress_ < 0x80u) dsp_.writeRegister(dspAddress_, value);
            break;
        case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F7:
            apuToCpu_[address - 0x00F4u] = value;
            break;
        case 0x00FA: case 0x00FB: case 0x00FC:
            timers_[address - 0x00FAu].target = value;
            break;
        default:
            break;
    }
}

void APU::writeControl(u8 value) {
    const u8 previous = control_;
    control_ = value;
    if (value & 0x10u) { cpuToApu_[0] = 0; cpuToApu_[1] = 0; }
    if (value & 0x20u) { cpuToApu_[2] = 0; cpuToApu_[3] = 0; }
    iplRomEnabled_ = (value & 0x80u) != 0;
    for (unsigned i = 0; i < 3; ++i) {
        const bool enabled = (value & (1u << i)) != 0;
        const bool wasEnabled = (previous & (1u << i)) != 0;
        if (enabled && !wasEnabled) {
            timers_[i].divider = 0;
            timers_[i].output = 0;
            // Stage 1 never resets on $F1 enable; only stages 2/3 do.
        }
        timers_[i].enabled = enabled;
    }
}

const std::vector<s16>& APU::pendingSamples() const { return dsp_.pendingSamples(); }
void APU::clearPendingSamples() { dsp_.clearPendingSamples(); }
const SPC700& APU::spc700() const { return spc700_; }
SPC700& APU::spc700() { return spc700_; }
const DSP& APU::dsp() const { return dsp_; }
DSP& APU::dsp() { return dsp_; }
u8 APU::cpuInputPort(unsigned port) const { return cpuToApu_[port & 3u]; }
bool APU::iplRomEnabled() const { return iplRomEnabled_; }
u64 APU::cycles() const { return cycles_; }
u8 APU::rawRamByte(u16 address) const { return ram_[address]; }

} // namespace snes
