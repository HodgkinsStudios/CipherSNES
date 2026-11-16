#ifndef SNES_CORE_SPC700_HPP
#define SNES_CORE_SPC700_HPP

#include "snes/Types.hpp"
#include "snes/core/Component.hpp"

namespace snes {

class APU;

class SPC700 : public Component {
    friend class SaveStateManager;
public:
    struct InstructionTrace {
        u16 pc;
        u8 opcode, a, x, y, sp, psw;
        InstructionTrace() : pc(0), opcode(0), a(0), x(0), y(0), sp(0), psw(0) {}
    };
    enum Flag {
        Carry = 0x01,
        Zero = 0x02,
        InterruptEnable = 0x04,
        HalfCarry = 0x08,
        Break = 0x10,
        DirectPage = 0x20,
        Overflow = 0x40,
        Negative = 0x80
    };

    SPC700();
    void connect(APU* apu);
    void reset();
    unsigned step();

    u16 pc() const;
    u8 a() const;
    u8 x() const;
    u8 y() const;
    u8 sp() const;
    u8 psw() const;
    u64 cycles() const;
    u64 instructions() const;
    bool stopped() const;
    unsigned recentInstructionTraceCount() const;
    const InstructionTrace& recentInstructionTrace(unsigned oldestIndex) const;

private:
    u8 read(u16 address, bool operand = false);
    void write(u16 address, u8 value);
    void movWrite(u16 address, u8 value);
    u8 fetch8();
    u16 fetch16();
    u16 dpAddress(u8 offset) const;
    u16 dpXAddress(u8 offset) const;
    u16 dpYAddress(u8 offset) const;
    u16 read16(u16 address);
    u16 read16Dp(u8 offset);
    void write16Dp(u8 offset, u16 value);
    void push(u8 value);
    u8 pull();
    void push16(u16 value);
    u16 pull16();
    bool flag(Flag value) const;
    void setFlag(Flag value, bool enabled);
    void setNZ(u8 value);
    void setNZ16(u16 value);
    void compare(u8 lhs, u8 rhs);
    u8 adc(u8 lhs, u8 rhs);
    u8 sbc(u8 lhs, u8 rhs);
    u8 applyAlu(unsigned operation, u8 lhs, u8 rhs, bool& writeResult);
    u8 applyShift(unsigned operation, u8 value);
    void branch(bool condition, unsigned& cyclesUsed);

    APU* apu_;
    u16 pc_;
    u8 a_;
    u8 x_;
    u8 y_;
    u8 sp_;
    u8 psw_;
    u64 cycles_;
    u64 instructions_;
    bool stopped_;
    InstructionTrace instructionTrace_[256];
    unsigned instructionTracePosition_;
    unsigned instructionTraceCount_;
};

} // namespace snes

#endif
