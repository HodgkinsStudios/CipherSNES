#include "snes/core/SPC700.hpp"
#include "snes/core/APU.hpp"

namespace snes {

SPC700::SPC700()
    : apu_(0), pc_(0xFFC0u), a_(0), x_(0), y_(0), sp_(0xEFu),
      psw_(0x02u), cycles_(0), instructions_(0), stopped_(false),
      instructionTrace_(), instructionTracePosition_(0), instructionTraceCount_(0) {}

void SPC700::connect(APU* apu) { apu_ = apu; }

void SPC700::reset() {
    pc_ = 0xFFC0u;
    a_ = x_ = y_ = 0;
    sp_ = 0xEFu;
    psw_ = 0x02u;
    cycles_ = 0;
    instructions_ = 0;
    stopped_ = false;
    instructionTracePosition_ = 0;
    instructionTraceCount_ = 0;
}

u8 SPC700::read(u16 address, bool operand) {
    if (!apu_) return 0xFFu;
    const u64 before = apu_->traceEnabled_ ? apu_->cycles_ : 0;
    const bool inputPort = (address >= 0x00F4u && address <= 0x00F7u) && apu_->spcInstructionActive_;
    if (inputPort) {
        ++apu_->spcBusCycles_;
        apu_->clockSpcInputPortHalf(address);
    } else apu_->clockSpcBusAccess(address);
    const u8 value = apu_->readSpcMemory(address);
    if (inputPort) apu_->clockSpcInputPortHalf(address);
    if (apu_->traceEnabled_) apu_->recordBusAccess(operand ? APU::BusKind::Operand : APU::BusKind::Read, address, value, before);
    return value;
}
void SPC700::write(u16 address, u8 value) {
    if (!apu_) return;
    const u64 before = apu_->traceEnabled_ ? apu_->cycles_ : 0;
    apu_->clockSpcBusAccess(address);
    apu_->writeSpcMemory(address, value);
    if (apu_->traceEnabled_) apu_->recordBusAccess(APU::BusKind::Write, address, value, before);
}
// Unlike an ordinary memory store, most SPC700 MOV-to-memory opcodes first
// issue a real read of the destination. That read has observable side effects
// for $FD-$FF (TnOUT read-and-clear), even though the subsequent write to
// those registers does nothing. MOV dp,dp and read-modify-write operations
// have their own bus sequence and must not use this helper.
void SPC700::movWrite(u16 address, u8 value) {
    (void)read(address);
    write(address, value);
}

u8 SPC700::fetch8() { const u8 value = read(pc_, true); ++pc_; return value; }
u16 SPC700::fetch16() { const u8 lo = fetch8(); return static_cast<u16>(lo | (static_cast<u16>(fetch8()) << 8)); }
u16 SPC700::dpAddress(u8 offset) const { return static_cast<u16>((psw_ & DirectPage ? 0x0100u : 0u) | offset); }
u16 SPC700::dpXAddress(u8 offset) const { return dpAddress(static_cast<u8>(offset + x_)); }
u16 SPC700::dpYAddress(u8 offset) const { return dpAddress(static_cast<u8>(offset + y_)); }
u16 SPC700::read16(u16 address) { const u8 lo = read(address); return static_cast<u16>(lo | (static_cast<u16>(read(static_cast<u16>(address + 1u))) << 8)); }
u16 SPC700::read16Dp(u8 offset) { const u8 lo = read(dpAddress(offset)); const u8 hi = read(dpAddress(static_cast<u8>(offset + 1u))); return static_cast<u16>(lo | (static_cast<u16>(hi) << 8)); }
void SPC700::write16Dp(u8 offset, u16 value) { write(dpAddress(offset), static_cast<u8>(value)); write(dpAddress(static_cast<u8>(offset + 1u)), static_cast<u8>(value >> 8)); }
void SPC700::push(u8 value) { write(static_cast<u16>(0x0100u | sp_), value); --sp_; }
u8 SPC700::pull() { ++sp_; return read(static_cast<u16>(0x0100u | sp_)); }
void SPC700::push16(u16 value) { push(static_cast<u8>(value >> 8)); push(static_cast<u8>(value)); }
u16 SPC700::pull16() { const u8 lo = pull(); return static_cast<u16>(lo | (static_cast<u16>(pull()) << 8)); }
bool SPC700::flag(Flag value) const { return (psw_ & value) != 0; }
void SPC700::setFlag(Flag value, bool enabled) { if (enabled) psw_ |= value; else psw_ &= static_cast<u8>(~value); }
void SPC700::setNZ(u8 value) { setFlag(Zero, value == 0); setFlag(Negative, (value & 0x80u) != 0); }
void SPC700::setNZ16(u16 value) { setFlag(Zero, value == 0); setFlag(Negative, (value & 0x8000u) != 0); }
void SPC700::compare(u8 lhs, u8 rhs) { const unsigned result = static_cast<unsigned>(lhs) - rhs; setFlag(Carry, lhs >= rhs); setNZ(static_cast<u8>(result)); }

u8 SPC700::adc(u8 lhs, u8 rhs) {
    const unsigned carry = flag(Carry) ? 1u : 0u;
    const unsigned result = static_cast<unsigned>(lhs) + rhs + carry;
    setFlag(Carry, result > 0xFFu);
    setFlag(HalfCarry, ((lhs & 0x0Fu) + (rhs & 0x0Fu) + carry) > 0x0Fu);
    setFlag(Overflow, ((~(lhs ^ rhs) & (lhs ^ static_cast<u8>(result))) & 0x80u) != 0);
    const u8 value = static_cast<u8>(result);
    setNZ(value);
    return value;
}

u8 SPC700::sbc(u8 lhs, u8 rhs) {
    const int borrow = flag(Carry) ? 0 : 1;
    const int result = static_cast<int>(lhs) - static_cast<int>(rhs) - borrow;
    const u8 value = static_cast<u8>(result);
    setFlag(Carry, result >= 0);
    setFlag(HalfCarry, static_cast<int>(lhs & 0x0Fu) - static_cast<int>(rhs & 0x0Fu) - borrow >= 0);
    setFlag(Overflow, (((lhs ^ rhs) & (lhs ^ value)) & 0x80u) != 0);
    setNZ(value);
    return value;
}

u8 SPC700::applyAlu(unsigned operation, u8 lhs, u8 rhs, bool& writeResult) {
    writeResult = true;
    switch (operation) {
        case 0: { const u8 value = static_cast<u8>(lhs | rhs); setNZ(value); return value; }
        case 1: { const u8 value = static_cast<u8>(lhs & rhs); setNZ(value); return value; }
        case 2: { const u8 value = static_cast<u8>(lhs ^ rhs); setNZ(value); return value; }
        case 3: compare(lhs, rhs); writeResult = false; return lhs;
        case 4: return adc(lhs, rhs);
        default: return sbc(lhs, rhs);
    }
}

u8 SPC700::applyShift(unsigned operation, u8 value) {
    u8 result = value;
    switch (operation) {
        case 0: setFlag(Carry, (value & 0x80u) != 0); result = static_cast<u8>(value << 1); break;
        case 1: { const bool oldCarry = flag(Carry); setFlag(Carry, (value & 0x80u) != 0); result = static_cast<u8>((value << 1) | (oldCarry ? 1u : 0u)); break; }
        case 2: setFlag(Carry, (value & 1u) != 0); result = static_cast<u8>(value >> 1); break;
        case 3: { const bool oldCarry = flag(Carry); setFlag(Carry, (value & 1u) != 0); result = static_cast<u8>((value >> 1) | (oldCarry ? 0x80u : 0u)); break; }
        case 4: result = static_cast<u8>(value - 1u); break;
        default: result = static_cast<u8>(value + 1u); break;
    }
    setNZ(result);
    return result;
}

void SPC700::branch(bool condition, unsigned& cyclesUsed) {
    const s8 displacement = static_cast<s8>(fetch8());
    if (condition) { pc_ = static_cast<u16>(pc_ + displacement); cyclesUsed += 2; }
}

unsigned SPC700::step() {
    if (stopped_) { ++cycles_; return 1; }

    InstructionTrace& trace = instructionTrace_[instructionTracePosition_];
    trace.pc = pc_; trace.a = a_; trace.x = x_; trace.y = y_; trace.sp = sp_; trace.psw = psw_;
    const u16 opcodeAddress = pc_;
    const u8 opcode = fetch8();
    // The initial instruction fetch is tagged separately from operand fetches.
    if (apu_ && apu_->traceEnabled_) apu_->retagLastOpcodeFetch(opcodeAddress);
    trace.opcode = opcode;
    instructionTracePosition_ = (instructionTracePosition_ + 1u) & 255u;
    if (instructionTraceCount_ < 256u) ++instructionTraceCount_;
    ++instructions_;
    unsigned used = 2;

    // TCALL n occupies every x1 opcode.
    if ((opcode & 0x0Fu) == 0x01u) {
        const unsigned n = opcode >> 4;
        push16(pc_);
        pc_ = read16(static_cast<u16>(0xFFDEu - n * 2u));
        used = 8;
        cycles_ += used;
        return used;
    }

    // SET1/CLR1 and BBS/BBC bit families.
    if ((opcode & 0x0Fu) == 0x02u) {
        const unsigned bit = opcode >> 5;
        const bool clear = (opcode & 0x10u) != 0;
        const u16 address = dpAddress(fetch8());
        u8 value = read(address);
        if (clear) value &= static_cast<u8>(~(1u << bit));
        else value |= static_cast<u8>(1u << bit);
        write(address, value);
        used = 4;
        cycles_ += used;
        return used;
    }
    if ((opcode & 0x0Fu) == 0x03u) {
        const unsigned bit = opcode >> 5;
        const bool branchOnClear = (opcode & 0x10u) != 0;
        const u8 value = read(dpAddress(fetch8()));
        const bool set = (value & (1u << bit)) != 0;
        used = 5;
        branch(branchOnClear ? !set : set, used);
        cycles_ += used;
        return used;
    }

    // Main OR/AND/EOR/CMP/ADC/SBC patterned groups.
    const unsigned aluGroup = opcode >> 5;
    const unsigned low = opcode & 0x1Fu;
    if (aluGroup <= 5u &&
        (low == 0x04u || low == 0x05u || low == 0x06u || low == 0x07u ||
         low == 0x08u || low == 0x09u || low == 0x14u || low == 0x15u ||
         low == 0x16u || low == 0x17u || low == 0x18u || low == 0x19u)) {
        u16 address = 0;
        u8 lhs = a_;
        u8 rhs = 0;
        bool destinationIsA = true;
        switch (low) {
            case 0x04: address = dpAddress(fetch8()); rhs = read(address); used = 3; break;
            case 0x05: address = fetch16(); rhs = read(address); used = 4; break;
            case 0x06: address = dpAddress(x_); rhs = read(address); used = 3; break;
            case 0x07: { const u8 dp = fetch8(); address = read16Dp(static_cast<u8>(dp + x_)); rhs = read(address); used = 6; break; }
            case 0x08: rhs = fetch8(); used = 2; break;
            case 0x09: { const u8 source = fetch8(); const u8 destination = fetch8(); address = dpAddress(destination); lhs = read(address); rhs = read(dpAddress(source)); destinationIsA = false; used = 6; break; }
            case 0x14: address = dpXAddress(fetch8()); rhs = read(address); used = 4; break;
            case 0x15: address = static_cast<u16>(fetch16() + x_); rhs = read(address); used = 5; break;
            case 0x16: address = static_cast<u16>(fetch16() + y_); rhs = read(address); used = 5; break;
            case 0x17: { const u8 dp = fetch8(); address = static_cast<u16>(read16Dp(dp) + y_); rhs = read(address); used = 6; break; }
            case 0x18: { rhs = fetch8(); address = dpAddress(fetch8()); lhs = read(address); destinationIsA = false; used = 5; break; }
            default: address = dpAddress(x_); lhs = read(address); rhs = read(dpAddress(y_)); destinationIsA = false; used = 5; break;
        }
        bool writeResult = false;
        const u8 result = applyAlu(aluGroup, lhs, rhs, writeResult);
        if (writeResult) {
            if (destinationIsA) a_ = result;
            else write(address, result);
        }
        cycles_ += used;
        return used;
    }

    // Shift/rotate/inc/dec patterned memory and accumulator groups.
    const unsigned shiftGroup = opcode >> 5;
    if (shiftGroup <= 5u &&
        (low == 0x0Bu || low == 0x0Cu || low == 0x1Bu || low == 0x1Cu)) {
        u16 address = 0;
        if (low == 0x1Cu) {
            a_ = applyShift(shiftGroup, a_);
            used = 2;
        } else {
            if (low == 0x0Bu) { address = dpAddress(fetch8()); used = 4; }
            else if (low == 0x0Cu) { address = fetch16(); used = 5; }
            else { address = dpXAddress(fetch8()); used = 5; }
            write(address, applyShift(shiftGroup, read(address)));
        }
        cycles_ += used;
        return used;
    }

    switch (opcode) {
        case 0x00: used = 2; break; // NOP
        case 0x0A: case 0x2A: case 0x4A: case 0x6A:
        case 0x8A: case 0xAA: case 0xCA: case 0xEA: {
            const u16 encoded = fetch16();
            const u16 address = encoded & 0x1FFFu;
            const unsigned bit = encoded >> 13;
            const bool memoryBit = (read(address) & (1u << bit)) != 0;
            if (opcode == 0x0A) setFlag(Carry, flag(Carry) || memoryBit);
            else if (opcode == 0x2A) setFlag(Carry, flag(Carry) || !memoryBit);
            else if (opcode == 0x4A) setFlag(Carry, flag(Carry) && memoryBit);
            else if (opcode == 0x6A) setFlag(Carry, flag(Carry) && !memoryBit);
            else if (opcode == 0x8A) setFlag(Carry, flag(Carry) != memoryBit);
            else if (opcode == 0xAA) setFlag(Carry, memoryBit);
            else if (opcode == 0xCA) {
                u8 value = read(address);
                if (flag(Carry)) value |= static_cast<u8>(1u << bit);
                else value &= static_cast<u8>(~(1u << bit));
                write(address, value);
            } else write(address, static_cast<u8>(read(address) ^ (1u << bit)));
            used = (opcode == 0xAA ? 4u : (opcode == 0xCA ? 6u : 5u));
            break;
        }
        case 0x0D: push(psw_); used = 4; break;
        case 0x0E: { // TSET1: N/Z reflect A-M, but C and the other flags are preserved.
            const u16 address = fetch16();
            const u8 value = read(address);
            setNZ(static_cast<u8>(a_ - value));
            write(address, static_cast<u8>(value | a_));
            used = 6; break;
        }
        case 0x0F: { // BRK pushes the pre-BRK PSW, then sets B internally.
            push16(pc_);
            push(psw_);
            setFlag(InterruptEnable, false);
            setFlag(Break, true);
            pc_ = read16(0xFFDEu);
            used = 8; break;
        }
        case 0x10: branch(!flag(Negative), used); break;
        case 0x1A: { const u8 dp = fetch8(); u16 value = static_cast<u16>(read16Dp(dp) - 1u); write16Dp(dp, value); setNZ16(value); used = 6; break; }
        case 0x1D: x_ = static_cast<u8>(x_ - 1u); setNZ(x_); used = 2; break;
        case 0x1E: compare(x_, read(fetch16())); used = 4; break;
        case 0x1F: { const u16 base = fetch16(); pc_ = read16(static_cast<u16>(base + x_)); used = 6; break; }
        case 0x20: setFlag(DirectPage, false); used = 2; break;
        case 0x2D: push(a_); used = 4; break;
        case 0x2E: { const u8 value = read(dpAddress(fetch8())); used = 5; branch(a_ != value, used); break; }
        case 0x2F: branch(true, used); used = 4; break;
        case 0x30: branch(flag(Negative), used); break;
        case 0x3A: { const u8 dp = fetch8(); u16 value = static_cast<u16>(read16Dp(dp) + 1u); write16Dp(dp, value); setNZ16(value); used = 6; break; }
        case 0x3D: x_ = static_cast<u8>(x_ + 1u); setNZ(x_); used = 2; break;
        case 0x3E: compare(x_, read(dpAddress(fetch8()))); used = 3; break;
        case 0x3F: { const u16 target = fetch16(); push16(pc_); pc_ = target; used = 8; break; }
        case 0x40: setFlag(DirectPage, true); used = 2; break;
        case 0x4D: push(x_); used = 4; break;
        case 0x4E: { // TCLR1 has the same flag behavior as TSET1.
            const u16 address = fetch16();
            const u8 value = read(address);
            setNZ(static_cast<u8>(a_ - value));
            write(address, static_cast<u8>(value & ~a_));
            used = 6; break;
        }
        case 0x4F: { const u8 target = fetch8(); push16(pc_); pc_ = static_cast<u16>(0xFF00u | target); used = 6; break; }
        case 0x50: branch(!flag(Overflow), used); break;
        case 0x5A: { const u8 dp = fetch8(); const u16 ya = static_cast<u16>((static_cast<u16>(y_) << 8) | a_); const u16 rhs = read16Dp(dp); const u32 result = static_cast<u32>(ya) - rhs; setFlag(Carry, ya >= rhs); setNZ16(static_cast<u16>(result)); used = 4; break; }
        case 0x5D: x_ = a_; setNZ(x_); used = 2; break;
        case 0x5E: compare(y_, read(fetch16())); used = 4; break;
        case 0x5F: pc_ = fetch16(); used = 3; break;
        case 0x60: setFlag(Carry, false); used = 2; break;
        case 0x6D: push(y_); used = 4; break;
        case 0x6E: { const u16 address = dpAddress(fetch8()); const u8 value = static_cast<u8>(read(address) - 1u); write(address, value); used = 5; branch(value != 0, used); break; }
        case 0x6F: pc_ = pull16(); used = 5; break;
        case 0x70: branch(flag(Overflow), used); break;
        case 0x7A: {
            const u8 dp = fetch8(); const u16 lhs = static_cast<u16>((static_cast<u16>(y_) << 8) | a_); const u16 rhs = read16Dp(dp);
            const u32 result = static_cast<u32>(lhs) + rhs;
            setFlag(Carry, result > 0xFFFFu); setFlag(HalfCarry, ((lhs & 0x0FFFu) + (rhs & 0x0FFFu)) > 0x0FFFu);
            setFlag(Overflow, ((~(lhs ^ rhs) & (lhs ^ static_cast<u16>(result))) & 0x8000u) != 0);
            a_ = static_cast<u8>(result); y_ = static_cast<u8>(result >> 8); setNZ16(static_cast<u16>(result)); used = 5; break;
        }
        case 0x7D: a_ = x_; setNZ(a_); used = 2; break;
        case 0x7E: compare(y_, read(dpAddress(fetch8()))); used = 3; break;
        case 0x7F: psw_ = pull(); pc_ = pull16(); used = 6; break;
        case 0x80: setFlag(Carry, true); used = 2; break;
        case 0x8B: { const u16 address = dpAddress(fetch8()); const u8 value = static_cast<u8>(read(address) - 1u); write(address, value); setNZ(value); used = 4; break; }
        case 0x8C: { const u16 address = fetch16(); const u8 value = static_cast<u8>(read(address) - 1u); write(address, value); setNZ(value); used = 5; break; }
        case 0x8D: y_ = fetch8(); setNZ(y_); used = 2; break;
        case 0x8E: psw_ = pull(); used = 4; break;
        case 0x8F: { const u8 immediate = fetch8(); const u16 address = dpAddress(fetch8()); movWrite(address, immediate); used = 5; break; }
        case 0x90: branch(!flag(Carry), used); break;
        case 0x9A: {
            const u8 dp = fetch8(); const u16 lhs = static_cast<u16>((static_cast<u16>(y_) << 8) | a_); const u16 rhs = read16Dp(dp);
            const s32 signedResult = static_cast<s32>(lhs) - rhs; const u16 result = static_cast<u16>(signedResult);
            setFlag(Carry, signedResult >= 0); setFlag(HalfCarry, static_cast<int>(lhs & 0x0FFFu) - static_cast<int>(rhs & 0x0FFFu) >= 0);
            setFlag(Overflow, (((lhs ^ rhs) & (lhs ^ result)) & 0x8000u) != 0);
            a_ = static_cast<u8>(result); y_ = static_cast<u8>(result >> 8); setNZ16(result); used = 5; break;
        }
        case 0x9B: { const u16 address = dpXAddress(fetch8()); const u8 value = static_cast<u8>(read(address) - 1u); write(address, value); setNZ(value); used = 5; break; }
        case 0x9C: a_ = static_cast<u8>(a_ - 1u); setNZ(a_); used = 2; break;
        case 0x9D: x_ = sp_; setNZ(x_); used = 2; break;
        case 0x9E: {
            const unsigned ya = (static_cast<unsigned>(y_) << 8) | a_;
            setFlag(HalfCarry, (y_ & 0x0Fu) >= (x_ & 0x0Fu));
            setFlag(Overflow, y_ >= x_);
            // S-SMP DIV is not a plain 16/8 division when the quotient would
            // overflow.  The alternate silicon path is used when Y >= 2*X,
            // including X=0.  Music engines use this behavior in pitch math.
            if (x_ != 0u && y_ < static_cast<unsigned>(x_) * 2u) {
                a_ = static_cast<u8>(ya / x_);
                y_ = static_cast<u8>(ya % x_);
            } else {
                const unsigned denominator = 0x100u - x_;
                const unsigned adjusted = ya - (static_cast<unsigned>(x_) << 9);
                a_ = static_cast<u8>(0xFFu - adjusted / denominator);
                y_ = static_cast<u8>(x_ + adjusted % denominator);
            }
            setNZ(a_); used = 12; break;
        }
        case 0x9F: a_ = static_cast<u8>((a_ << 4) | (a_ >> 4)); setNZ(a_); used = 5; break;
        case 0xA0: setFlag(InterruptEnable, true); used = 3; break;
        case 0xAB: { const u16 address = dpAddress(fetch8()); const u8 value = static_cast<u8>(read(address) + 1u); write(address, value); setNZ(value); used = 4; break; }
        case 0xAC: { const u16 address = fetch16(); const u8 value = static_cast<u8>(read(address) + 1u); write(address, value); setNZ(value); used = 5; break; }
        case 0xAD: compare(y_, fetch8()); used = 2; break;
        case 0xAE: a_ = pull(); used = 4; break;
        case 0xAF: movWrite(dpAddress(x_), a_); ++x_; used = 4; break;
        case 0xB0: branch(flag(Carry), used); break;
        case 0xBA: { const u16 value = read16Dp(fetch8()); a_ = static_cast<u8>(value); y_ = static_cast<u8>(value >> 8); setNZ16(value); used = 5; break; }
        case 0xBB: { const u16 address = dpXAddress(fetch8()); const u8 value = static_cast<u8>(read(address) + 1u); write(address, value); setNZ(value); used = 5; break; }
        case 0xBC: a_ = static_cast<u8>(a_ + 1u); setNZ(a_); used = 2; break;
        case 0xBD: sp_ = x_; used = 2; break;
        case 0xBE: {
            if (!flag(Carry) || a_ > 0x99u) { a_ = static_cast<u8>(a_ - 0x60u); setFlag(Carry, false); }
            if (!flag(HalfCarry) || (a_ & 0x0Fu) > 9u) a_ = static_cast<u8>(a_ - 6u);
            setNZ(a_); used = 3; break;
        }
        case 0xBF: a_ = read(dpAddress(x_)); ++x_; setNZ(a_); used = 4; break;
        case 0xC0: setFlag(InterruptEnable, false); used = 3; break;
        case 0xC4: movWrite(dpAddress(fetch8()), a_); used = 4; break;
        case 0xC5: movWrite(fetch16(), a_); used = 5; break;
        case 0xC6: movWrite(dpAddress(x_), a_); used = 4; break;
        case 0xC7: { const u8 dp = fetch8(); movWrite(read16Dp(static_cast<u8>(dp + x_)), a_); used = 7; break; }
        case 0xC8: compare(x_, fetch8()); used = 2; break;
        case 0xC9: movWrite(fetch16(), x_); used = 5; break;
        case 0xCB: movWrite(dpAddress(fetch8()), y_); used = 4; break;
        case 0xCC: movWrite(fetch16(), y_); used = 5; break;
        case 0xCD: x_ = fetch8(); setNZ(x_); used = 2; break;
        case 0xCE: x_ = pull(); used = 4; break;
        case 0xCF: { const u16 product = static_cast<u16>(a_ * y_); a_ = static_cast<u8>(product); y_ = static_cast<u8>(product >> 8); setNZ(y_); used = 9; break; }
        case 0xD0: branch(!flag(Zero), used); break;
        case 0xD4: movWrite(dpXAddress(fetch8()), a_); used = 5; break;
        case 0xD5: movWrite(static_cast<u16>(fetch16() + x_), a_); used = 6; break;
        case 0xD6: movWrite(static_cast<u16>(fetch16() + y_), a_); used = 6; break;
        case 0xD7: { const u8 dp = fetch8(); movWrite(static_cast<u16>(read16Dp(dp) + y_), a_); used = 7; break; }
        case 0xD8: movWrite(dpAddress(fetch8()), x_); used = 4; break;
        case 0xD9: movWrite(dpYAddress(fetch8()), x_); used = 5; break;
        case 0xDA: { const u8 dp = fetch8(); (void)read(dpAddress(dp)); write16Dp(dp, static_cast<u16>((static_cast<u16>(y_) << 8) | a_)); used = 5; break; }
        case 0xDB: movWrite(dpXAddress(fetch8()), y_); used = 5; break;
        case 0xDC: y_ = static_cast<u8>(y_ - 1u); setNZ(y_); used = 2; break;
        case 0xDD: a_ = y_; setNZ(a_); used = 2; break;
        case 0xDE: { const u8 value = read(dpXAddress(fetch8())); used = 6; branch(a_ != value, used); break; }
        case 0xDF: {
            if (flag(Carry) || a_ > 0x99u) { a_ = static_cast<u8>(a_ + 0x60u); setFlag(Carry, true); }
            if (flag(HalfCarry) || (a_ & 0x0Fu) > 9u) a_ = static_cast<u8>(a_ + 6u);
            setNZ(a_); used = 3; break;
        }
        case 0xE0: setFlag(Overflow, false); setFlag(HalfCarry, false); used = 2; break;
        case 0xE4: a_ = read(dpAddress(fetch8())); setNZ(a_); used = 3; break;
        case 0xE5: a_ = read(fetch16()); setNZ(a_); used = 4; break;
        case 0xE6: a_ = read(dpAddress(x_)); setNZ(a_); used = 3; break;
        case 0xE7: { const u8 dp = fetch8(); a_ = read(read16Dp(static_cast<u8>(dp + x_))); setNZ(a_); used = 6; break; }
        case 0xE8: a_ = fetch8(); setNZ(a_); used = 2; break;
        case 0xE9: x_ = read(fetch16()); setNZ(x_); used = 4; break;
        case 0xEB: y_ = read(dpAddress(fetch8())); setNZ(y_); used = 3; break;
        case 0xEC: y_ = read(fetch16()); setNZ(y_); used = 4; break;
        case 0xED: setFlag(Carry, !flag(Carry)); used = 3; break;
        case 0xEE: y_ = pull(); used = 4; break;
        case 0xEF: stopped_ = true; used = 3; break;
        case 0xF0: branch(flag(Zero), used); break;
        case 0xF4: a_ = read(dpXAddress(fetch8())); setNZ(a_); used = 4; break;
        case 0xF5: a_ = read(static_cast<u16>(fetch16() + x_)); setNZ(a_); used = 5; break;
        case 0xF6: a_ = read(static_cast<u16>(fetch16() + y_)); setNZ(a_); used = 5; break;
        case 0xF7: { const u8 dp = fetch8(); a_ = read(static_cast<u16>(read16Dp(dp) + y_)); setNZ(a_); used = 6; break; }
        case 0xF8: x_ = read(dpAddress(fetch8())); setNZ(x_); used = 3; break;
        case 0xF9: x_ = read(dpYAddress(fetch8())); setNZ(x_); used = 4; break;
        case 0xFA: { const u8 source = fetch8(); const u8 destination = fetch8(); write(dpAddress(destination), read(dpAddress(source))); used = 5; break; }
        case 0xFB: y_ = read(dpXAddress(fetch8())); setNZ(y_); used = 4; break;
        case 0xFC: y_ = static_cast<u8>(y_ + 1u); setNZ(y_); used = 2; break;
        case 0xFD: y_ = a_; setNZ(y_); used = 2; break;
        case 0xFE: y_ = static_cast<u8>(y_ - 1u); /* DBNZ Y does not modify PSW.N/Z. */ used = 4; branch(y_ != 0, used); break;
        case 0xFF: stopped_ = true; used = 3; break;
        default:
            // All legal opcode values are handled by either a family above or
            // an explicit case. Keep an inert fallback for corrupted states.
            used = 2;
            break;
    }

    cycles_ += used;
    return used;
}

u16 SPC700::pc() const { return pc_; }
u8 SPC700::a() const { return a_; }
u8 SPC700::x() const { return x_; }
u8 SPC700::y() const { return y_; }
u8 SPC700::sp() const { return sp_; }
u8 SPC700::psw() const { return psw_; }
u64 SPC700::cycles() const { return cycles_; }
u64 SPC700::instructions() const { return instructions_; }
bool SPC700::stopped() const { return stopped_; }
unsigned SPC700::recentInstructionTraceCount() const { return instructionTraceCount_; }
const SPC700::InstructionTrace& SPC700::recentInstructionTrace(unsigned oldestIndex) const {
    static const InstructionTrace empty;
    if (oldestIndex >= instructionTraceCount_) return empty;
    const unsigned oldest = (instructionTracePosition_ + 256u - instructionTraceCount_) & 255u;
    return instructionTrace_[(oldest + oldestIndex) & 255u];
}

} // namespace snes
