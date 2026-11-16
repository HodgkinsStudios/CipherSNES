#ifndef SNES_CORE_APU_HPP
#define SNES_CORE_APU_HPP

#include <vector>
#include "snes/Types.hpp"
#include "snes/core/Component.hpp"
#include "snes/core/DSP.hpp"
#include "snes/core/SPC700.hpp"

namespace snes {

class APU : public Component {
    friend class SaveStateManager;
    friend class SPC700;
public:
    // CP31 diagnostic-only bus-phase stream. Does not affect public Core.hpp,
    // save state format, or emulated clock rates. Disabled by default.
    enum class BusKind : u8 { Opcode, Operand, Read, Write, Idle };
    enum class BusDomain : u8 { Aram, IplRom, Io, Internal };
    struct BusEvent {
        u64 sequence;
        u64 before, after; // actual APU hardware clocks
        u16 instructionPc, address;
        u8 value;
        BusKind kind;
        BusDomain domain;
        u8 testRegister;
    };
    void enableBusTrace(bool enable, unsigned capacity = 8192);
    std::vector<BusEvent> busTrace() const; // chronological, bounded snapshot
    u64 busTraceDropped() const;

    APU();
    void reset();
    bool loadIpl(const std::vector<u8>& data);
    void step();
    void stepCycles(unsigned cycles);
    u8 readCpuPort(unsigned port) const;
    void writeCpuPort(unsigned port, u8 value);
    const std::vector<s16>& pendingSamples() const;
    void clearPendingSamples();

    const SPC700& spc700() const;
    SPC700& spc700();
    const DSP& dsp() const;
    DSP& dsp();

    // Runtime diagnostics used by the frontend and self-tests.
    u8 cpuInputPort(unsigned port) const;
    bool iplRomEnabled() const;
    u64 cycles() const;
    u8 rawRamByte(u16 address) const;

    // Exposed for diagnostics and self-tests.
    u8 readSpcMemory(u16 address);
    void writeSpcMemory(u16 address, u8 value);

private:
    struct Timer {
        Timer();
        bool enabled;
        u8 target;
        u16 divider;
        u8 output;
        unsigned baseCounter;
    };

    void advanceHardware(unsigned cycles, unsigned timerCycles);
    void advanceHardware(unsigned cycles) { advanceHardware(cycles, cycles); }
    void clockSpcBusAccess(u16 address);
    // Sample S-SMP input ports between two half bus waits.
    void clockSpcInputPortHalf(u16 address);
    void recordBusAccess(BusKind kind, u16 address, u8 value, u64 before);
    void recordIdle(unsigned cycles, u64 before);
    void appendTrace(const BusEvent& event);
    void retagLastOpcodeFetch(u16 address);
    BusDomain domainOf(u16 address) const;

    unsigned advanceSpcCpuCycles(unsigned cycles, BusDomain domain);
    void writeControl(u8 value);
    void advanceTimer(Timer& timer, unsigned divider, unsigned cycles);

    bool traceEnabled_;
    unsigned traceCapacity_, traceWrite_, traceCount_;
    u64 traceSequence_, traceDropped_;
    u16 traceInstructionPc_;
    std::vector<BusEvent> traceRing_;

    u64 cycles_;
    s32 executionBudget_;
    unsigned dspClockRemainder_;
    unsigned slowCycleRemainder_;
    bool spcInstructionActive_;
    unsigned spcBusCycles_;
    unsigned portCpuHalfRemainder_, portTimerHalfRemainder_;
    u8 ram_[65536];
    u8 iplRom_[64];
    u8 cpuToApu_[4];
    u8 apuToCpu_[4];
    u8 control_;
    u8 test_;
    u8 dspAddress_;
    bool iplRomEnabled_;
    Timer timers_[3];
    SPC700 spc700_;
    DSP dsp_;
};

} // namespace snes

#endif
