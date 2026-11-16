#ifndef SNES_CORE_CPU65816_HPP
#define SNES_CORE_CPU65816_HPP

#include "snes/Types.hpp"
#include "snes/core/Component.hpp"

namespace snes {

class CPU65816Bus;

class CPU65816 : public Component {
    friend class SaveStateManager;
public:
    struct InstructionTrace {
        u32 address;
        u8 opcode;
        u16 a, x, y, s, d;
        u8 pbr, dbr, p;
        bool emulation;
        InstructionTrace() : address(0), opcode(0), a(0), x(0), y(0), s(0), d(0),
                             pbr(0), dbr(0), p(0), emulation(true) {}
    };
    enum Flag {
        Carry = 0x01, Zero = 0x02, IrqDisable = 0x04, Decimal = 0x08,
        Index8 = 0x10, Accumulator8 = 0x20, Overflow = 0x40, Negative = 0x80
    };

    CPU65816();
    void connectBus(CPU65816Bus* bus);
    void reset();
    unsigned step();
    void requestNmi();
    void requestIrq();
    void setIrqLine(bool asserted);
    // Latch the IRQ eligibility on the penultimate instruction cycle.
    // The independently driven live line still wakes WAI.
    void setInstructionIrqPollEnabled(bool enabled);
    void latchInstructionIrqPoll(bool asserted);
    bool latchedInstructionIrqPending() const;
    void setInstructionNmiPollEnabled(bool enabled);
    void latchInstructionNmiPoll(bool asserted);
    bool latchedInstructionNmiPending() const;

    u16 a() const;
    u16 x() const;
    u16 y() const;
    u16 s() const;
    u16 d() const;
    u16 pc() const;
    u8 pbr() const;
    u8 dbr() const;
    u8 p() const;
    u8 lastExecutedOpcode() const;
    bool emulationMode() const;
    bool stopped() const;
    bool waiting() const;
    bool irqLineAsserted() const;
    bool nmiPending() const;
    u32 dominantRecentProgramCounter(unsigned& hits) const;
    unsigned recentInstructionTraceCount() const;
    const InstructionTrace& recentInstructionTrace(unsigned oldestIndex) const;
    u16 stackLowWaterMark() const;
    bool stackPageWasOverwritten() const;
    u32 lastStackPageWritePc() const;
    u32 lastStackPageWriteAddress() const;
    u8 lastStackPageWriteValue() const;
    u8 lastStackPageWriteOpcode() const;
    u32 lastGameModeWritePc() const;
    u8 lastGameModeWriteValue() const;
    u8 lastGameModeWriteOpcode() const;
    u16 lastInterruptVector() const;
    u32 lastInterruptSourcePc() const;
    u16 lastInterruptTarget() const;
    u64 cycles() const;
    u64 instructions() const;

private:
    u8 read8(u32 address);
    u16 read16(u32 address);
    u32 read24(u32 address);
    void write8(u32 address, u8 value);
    void write16(u32 address, u16 value);
    u8 fetch8();
    u16 fetch16();
    u32 fetch24();

    bool m8() const;
    bool x8() const;
    void setFlag(Flag flag, bool value);
    bool flag(Flag flag) const;
    void setP(u8 value);
    void setNZ(u16 value, bool eightBit);
    u16 readM(u32 address);
    u16 readMLong(u32 address);
    void writeM(u32 address, u16 value);
    void writeMLong(u32 address, u16 value);
    u16 readIndex(u32 address);

    u32 addrDp();
    u32 addrDpX();
    u32 addrDpY();
    u32 addrDpIndirect();
    u32 addrDpIndirectX();
    u32 addrDpIndirectY();
    u32 addrDpLong();
    u32 addrDpLongY();
    u32 addrStackRelative();
    u32 addrStackIndirectY();
    u32 addrAbs();
    u32 addrAbsX();
    u32 addrAbsY();
    u32 addrLong();
    u32 addrLongX();

    void push8(u8 value);
    void push16(u16 value);
    void push16Linear(u16 value);
    u8 pull8();
    u16 pull16();
    u16 pull16Linear();
    u32 pull24Linear();
    void interrupt(u16 vector, bool software);
    void branch(bool condition);
    void branchLong();

    void opOra(u16 value);
    void opAnd(u16 value);
    void opEor(u16 value);
    void opAdc(u16 value);
    void opSbc(u16 value);
    void opCmp(u16 lhs, u16 rhs, bool eightBit);
    void opBit(u16 value, bool immediate);
    void opLoadA(u16 value);
    void opLoadX(u16 value);
    void opLoadY(u16 value);
    u16 opAsl(u16 value, bool eightBit);
    u16 opLsr(u16 value, bool eightBit);
    u16 opRol(u16 value, bool eightBit);
    u16 opRor(u16 value, bool eightBit);
    u16 opInc(u16 value, bool eightBit);
    u16 opDec(u16 value, bool eightBit);
    u16 decimalAdd(u16 lhs, u16 rhs, bool eightBit, bool subtract);

    CPU65816Bus* bus_;
    u16 a_, x_, y_, s_, d_, pc_;
    u8 pbr_, dbr_, p_;
    bool emulationMode_;
    bool nmiPending_;
    bool irqPending_;
    bool instructionIrqPollEnabled_;
    bool sampledInstructionIrq_;
    bool instructionNmiPollEnabled_;
    bool sampledInstructionNmi_;
    bool waiting_;
    bool stopped_;
    u64 cycles_;
    u64 instructions_;
    u32 recentProgramCounters_[256];
    unsigned recentProgramCounterPosition_;
    unsigned recentProgramCounterCount_;
    InstructionTrace instructionTrace_[512];
    unsigned instructionTracePosition_;
    unsigned instructionTraceCount_;
    u32 currentInstructionAddress_;
    u8 currentOpcode_;
    u16 stackLowWaterMark_;
    bool stackPageWasOverwritten_;
    u32 lastStackPageWritePc_;
    u32 lastStackPageWriteAddress_;
    u8 lastStackPageWriteValue_;
    u8 lastStackPageWriteOpcode_;
    u32 lastGameModeWritePc_;
    u8 lastGameModeWriteValue_;
    u8 lastGameModeWriteOpcode_;
    u16 lastInterruptVector_;
    u32 lastInterruptSourcePc_;
    u16 lastInterruptTarget_;
    bool branchTaken_;
    bool branchPageCrossed_;
    bool indexedPageCrossed_;
};

} // namespace snes

#endif
