#include "snes/core/CPU65816.hpp"
#include "snes/core/CPU65816Bus.hpp"
#include "snes/core/OpcodeTiming.hpp"

namespace snes {

CPU65816::CPU65816()
    : bus_(0), a_(0), x_(0), y_(0), s_(0x01FFu), d_(0), pc_(0),
      pbr_(0), dbr_(0), p_(0x34u), emulationMode_(true), nmiPending_(false),
      irqPending_(false), instructionIrqPollEnabled_(false), sampledInstructionIrq_(false), instructionNmiPollEnabled_(false), sampledInstructionNmi_(false), waiting_(false), stopped_(false), cycles_(0), instructions_(0),
      recentProgramCounters_(), recentProgramCounterPosition_(0), recentProgramCounterCount_(0),
      instructionTrace_(), instructionTracePosition_(0), instructionTraceCount_(0),
      currentInstructionAddress_(0), currentOpcode_(0), stackLowWaterMark_(0x01FFu),
      stackPageWasOverwritten_(false), lastStackPageWritePc_(0),
      lastStackPageWriteAddress_(0), lastStackPageWriteValue_(0), lastStackPageWriteOpcode_(0),
      lastGameModeWritePc_(0), lastGameModeWriteValue_(0), lastGameModeWriteOpcode_(0),
      lastInterruptVector_(0), lastInterruptSourcePc_(0), lastInterruptTarget_(0),
      branchTaken_(false), branchPageCrossed_(false), indexedPageCrossed_(false) {}

void CPU65816::connectBus(CPU65816Bus* bus) { bus_ = bus; }

void CPU65816::reset() {
    a_ = x_ = y_ = d_ = 0;
    s_ = 0x01FFu;
    pbr_ = dbr_ = 0;
    p_ = static_cast<u8>(IrqDisable | Accumulator8 | Index8);
    emulationMode_ = true;
    nmiPending_ = false;
    irqPending_ = false;
    sampledInstructionIrq_ = false;
    sampledInstructionNmi_ = false;
    waiting_ = false;
    stopped_ = false;
    cycles_ = 0;
    instructions_ = 0;
    recentProgramCounterPosition_ = 0;
    recentProgramCounterCount_ = 0;
    for (unsigned i = 0; i < 256u; ++i) recentProgramCounters_[i] = 0;
    instructionTracePosition_ = 0;
    instructionTraceCount_ = 0;
    currentInstructionAddress_ = 0;
    currentOpcode_ = 0;
    stackLowWaterMark_ = s_;
    stackPageWasOverwritten_ = false;
    lastStackPageWritePc_ = 0;
    lastStackPageWriteAddress_ = 0;
    lastStackPageWriteValue_ = 0;
    lastStackPageWriteOpcode_ = 0;
    lastGameModeWritePc_ = 0;
    lastGameModeWriteValue_ = 0;
    lastGameModeWriteOpcode_ = 0;
    lastInterruptVector_ = 0;
    lastInterruptSourcePc_ = 0;
    lastInterruptTarget_ = 0;
    branchTaken_ = false;
    branchPageCrossed_ = false;
    indexedPageCrossed_ = false;
    pc_ = bus_ ? read16(0x00FFFCu) : 0;
}

u8 CPU65816::read8(u32 address) { return bus_ ? bus_->read(address & 0xFFFFFFu) : 0xFFu; }
u16 CPU65816::read16(u32 address) {
    const u8 low = read8(address);
    const u32 next = (address & 0xFF0000u) | static_cast<u16>(address + 1u);
    return static_cast<u16>(low | (static_cast<u16>(read8(next)) << 8));
}
u32 CPU65816::read24(u32 address) {
    const u32 low = read16(address);
    const u32 highAddress = (address & 0xFF0000u) | static_cast<u16>(address + 2u);
    return low | (static_cast<u32>(read8(highAddress)) << 16);
}
void CPU65816::write8(u32 address, u8 value) {
    address &= 0xFFFFFFu;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 offset = static_cast<u16>(address);
    const bool lowMirror = ((bank <= 0x3Fu || (bank >= 0x80u && bank <= 0xBFu)) && offset == 0x0100u);
    const bool directWram = bank == 0x7Eu && offset == 0x0100u;
    if (lowMirror || directWram) {
        lastGameModeWritePc_ = currentInstructionAddress_;
        lastGameModeWriteValue_ = value;
        lastGameModeWriteOpcode_ = currentOpcode_;
    }
    if (bus_) bus_->write(address, value);
}
void CPU65816::write16(u32 address, u16 value) {
    write8(address, static_cast<u8>(value));
    write8((address & 0xFF0000u) | static_cast<u16>(address + 1u), static_cast<u8>(value >> 8));
}

u8 CPU65816::fetch8() {
    const u8 value = read8((static_cast<u32>(pbr_) << 16) | pc_);
    ++pc_;
    return value;
}
u16 CPU65816::fetch16() { const u8 lo = fetch8(); return static_cast<u16>(lo | (static_cast<u16>(fetch8()) << 8)); }
u32 CPU65816::fetch24() { const u32 lo = fetch16(); return lo | (static_cast<u32>(fetch8()) << 16); }

bool CPU65816::m8() const { return emulationMode_ || (p_ & Accumulator8) != 0; }
bool CPU65816::x8() const { return emulationMode_ || (p_ & Index8) != 0; }
void CPU65816::setFlag(Flag flagValue, bool value) { if (value) p_ |= flagValue; else p_ &= static_cast<u8>(~flagValue); }
bool CPU65816::flag(Flag flagValue) const { return (p_ & flagValue) != 0; }
void CPU65816::setP(u8 value) {
    p_ = value;
    if (emulationMode_) p_ |= static_cast<u8>(Accumulator8 | Index8);
    if (x8()) { x_ &= 0x00FFu; y_ &= 0x00FFu; }
}
void CPU65816::setNZ(u16 value, bool eightBit) {
    const u16 mask = eightBit ? 0x00FFu : 0xFFFFu;
    const u16 sign = eightBit ? 0x0080u : 0x8000u;
    value &= mask;
    setFlag(Zero, value == 0);
    setFlag(Negative, (value & sign) != 0);
}

u16 CPU65816::readM(u32 address) {
    if (m8()) return read8(address);
    const u8 low = read8(address);
    // A 16-bit data operand crosses the bank boundary; pointer-fetch
    // helpers such as read16() retain their own bank-local wrap behavior.
    const u32 high = (address + 1u) & 0xFFFFFFu;
    return static_cast<u16>(low | (static_cast<u16>(read8(high)) << 8));
}
u16 CPU65816::readMLong(u32 address) {
    if (m8()) return read8(address);
    const u8 low = read8(address);
    return static_cast<u16>(low | (static_cast<u16>(read8((address + 1u) & 0xFFFFFFu)) << 8));
}
void CPU65816::writeM(u32 address, u16 value) {
    if (m8()) { write8(address, static_cast<u8>(value)); return; }
    write8(address, static_cast<u8>(value));
    write8((address + 1u) & 0xFFFFFFu, static_cast<u8>(value >> 8));
}
void CPU65816::writeMLong(u32 address, u16 value) {
    if (m8()) write8(address, static_cast<u8>(value));
    else {
        write8(address, static_cast<u8>(value));
        write8((address + 1u) & 0xFFFFFFu, static_cast<u8>(value >> 8));
    }
}
u16 CPU65816::readIndex(u32 address) {
    if (x8()) return read8(address);
    const u8 low = read8(address);
    return static_cast<u16>(low | (static_cast<u16>(read8((address+1u)&0xFFFFFFu))<<8));
}

u32 CPU65816::addrDp() { return static_cast<u16>(d_ + fetch8()); }
u32 CPU65816::addrDpX() {
    const u8 operand = fetch8();
    const u16 index = x8() ? static_cast<u16>(x_ & 0x00FFu) : x_;
    // In emulation mode, indexed direct-page addressing wraps inside the
    // direct-page bank whenever D.l is zero.  Several commercial startup
    // routines depend on this 6502-compatible page-wrap behavior.
    if (emulationMode_ && (d_ & 0x00FFu) == 0u)
        return static_cast<u16>((d_ & 0xFF00u) | static_cast<u8>(operand + index));
    return static_cast<u16>(d_ + operand + index);
}
u32 CPU65816::addrDpY() {
    const u8 operand = fetch8();
    const u16 index = x8() ? static_cast<u16>(y_ & 0x00FFu) : y_;
    if (emulationMode_ && (d_ & 0x00FFu) == 0u)
        return static_cast<u16>((d_ & 0xFF00u) | static_cast<u8>(operand + index));
    return static_cast<u16>(d_ + operand + index);
}
u32 CPU65816::addrDpIndirect() {
    const u16 pointer = static_cast<u16>(d_ + fetch8());
    const u8 low = read8(pointer);
    const u16 highAddress = (emulationMode_ && (d_ & 0x00FFu) == 0u)
        ? static_cast<u16>((pointer & 0xFF00u) | static_cast<u8>(pointer + 1u))
        : static_cast<u16>(pointer + 1u);
    return (static_cast<u32>(dbr_) << 16) |
        static_cast<u16>(low | (static_cast<u16>(read8(highAddress)) << 8));
}
u32 CPU65816::addrDpIndirectX() {
    const u8 operand = fetch8();
    const u16 index = x8() ? static_cast<u16>(x_ & 0x00FFu) : x_;
    u16 pointer;
    if (emulationMode_ && (d_ & 0x00FFu) == 0u)
        pointer = static_cast<u16>((d_ & 0xFF00u) | static_cast<u8>(operand + index));
    else
        pointer = static_cast<u16>(d_ + operand + index);

    const u8 low = read8(pointer);
    // The 65C816 retains a hardware quirk in emulation mode: the pointer
    // calculation above does not wrap when D has a non-zero low byte, but the
    // second pointer-byte access does wrap inside the calculated page.
    const u16 highAddress = emulationMode_
        ? static_cast<u16>((pointer & 0xFF00u) | static_cast<u8>(pointer + 1u))
        : static_cast<u16>(pointer + 1u);
    return (static_cast<u32>(dbr_) << 16) |
        static_cast<u16>(low | (static_cast<u16>(read8(highAddress)) << 8));
}
u32 CPU65816::addrDpIndirectY() {
    const u16 pointer = static_cast<u16>(d_ + fetch8());
    const u8 low = read8(pointer);
    const u16 highAddress = (emulationMode_ && (d_ & 0x00FFu) == 0u)
        ? static_cast<u16>((pointer & 0xFF00u) | static_cast<u8>(pointer + 1u))
        : static_cast<u16>(pointer + 1u);
    const u16 base = static_cast<u16>(low | (static_cast<u16>(read8(highAddress)) << 8));
    const u16 index = x8() ? static_cast<u16>(y_ & 0xFFu) : y_;
    const u32 sum = static_cast<u32>(base) + index;
    indexedPageCrossed_ = (base & 0xFF00u) != (sum & 0xFF00u);
    // Indexed data addressing adds to a 24-bit effective address, carrying
    // into the next data bank instead of wrapping back within the DBR.
    return ((static_cast<u32>(dbr_) << 16) + sum) & 0xFFFFFFu;
}
u32 CPU65816::addrDpLong() {
    const u16 pointer = static_cast<u16>(d_ + fetch8());
    return read24(pointer);
}
u32 CPU65816::addrDpLongY() { return (addrDpLong() + (x8() ? (y_ & 0xFFu) : y_)) & 0xFFFFFFu; }
u32 CPU65816::addrStackRelative() { return static_cast<u16>(s_ + fetch8()); }
u32 CPU65816::addrStackIndirectY() {
    const u16 pointer = static_cast<u16>(s_ + fetch8());
    const u16 base = read16(pointer);
    const u32 sum = static_cast<u32>(base) + (x8() ? (y_ & 0xFFu) : y_);
    return ((static_cast<u32>(dbr_) << 16) + sum) & 0xFFFFFFu;
}
u32 CPU65816::addrAbs() { return (static_cast<u32>(dbr_) << 16) | fetch16(); }
u32 CPU65816::addrAbsX() {
    const u16 base = fetch16();
    const u32 sum = static_cast<u32>(base) + (x8() ? (x_ & 0xFFu) : x_);
    indexedPageCrossed_ = (base & 0xFF00u) != (sum & 0xFF00u);
    return ((static_cast<u32>(dbr_) << 16) + sum) & 0xFFFFFFu;
}
u32 CPU65816::addrAbsY() {
    const u16 base = fetch16();
    const u32 sum = static_cast<u32>(base) + (x8() ? (y_ & 0xFFu) : y_);
    indexedPageCrossed_ = (base & 0xFF00u) != (sum & 0xFF00u);
    return ((static_cast<u32>(dbr_) << 16) + sum) & 0xFFFFFFu;
}
u32 CPU65816::addrLong() { return fetch24(); }
u32 CPU65816::addrLongX() { return (fetch24() + (x8() ? (x_ & 0xFFu) : x_)) & 0xFFFFFFu; }

void CPU65816::push8(u8 value) {
    if (s_ < stackLowWaterMark_) stackLowWaterMark_ = s_;
    if (s_ == 0x0100u) {
        stackPageWasOverwritten_ = true;
        lastStackPageWritePc_ = currentInstructionAddress_;
        lastStackPageWriteAddress_ = s_;
        lastStackPageWriteValue_ = value;
        lastStackPageWriteOpcode_ = currentOpcode_;
    }
    write8(s_, value);
    if (emulationMode_) s_ = static_cast<u16>(0x0100u | ((s_ - 1u) & 0x00FFu));
    else --s_;
    if (s_ < stackLowWaterMark_) stackLowWaterMark_ = s_;
}
void CPU65816::push16(u16 value) { push8(static_cast<u8>(value >> 8)); push8(static_cast<u8>(value)); }
void CPU65816::push16Linear(u16 value) {
    if (!emulationMode_) { push16(value); return; }
    // PEA/PEI/PER/PHD and JSR (abs,X) perform a two-byte transfer whose
    // *memory addresses* can cross from $0100 to $00ff. JSR abs uses the
    // traditional 6502 $01ff emulation stack page wrap instead.
    if (s_ < stackLowWaterMark_) stackLowWaterMark_ = s_;
    if (s_ == 0x0100u) {
        stackPageWasOverwritten_ = true;
        lastStackPageWritePc_ = currentInstructionAddress_;
        lastStackPageWriteAddress_ = s_;
        lastStackPageWriteValue_ = static_cast<u8>(value >> 8);
        lastStackPageWriteOpcode_ = currentOpcode_;
    }
    write8(s_, static_cast<u8>(value >> 8));
    write8(static_cast<u16>(s_ - 1u), static_cast<u8>(value));
    s_ = static_cast<u16>(0x0100u | static_cast<u8>(s_ - 2u));
    if (s_ < stackLowWaterMark_) stackLowWaterMark_ = s_;
}
u8 CPU65816::pull8() {
    if (emulationMode_) s_ = static_cast<u16>(0x0100u | ((s_ + 1u) & 0x00FFu));
    else ++s_;
    return read8(s_);
}
u16 CPU65816::pull16() { const u8 lo = pull8(); return static_cast<u16>(lo | (static_cast<u16>(pull8()) << 8)); }
u16 CPU65816::pull16Linear() {
    if (!emulationMode_) return pull16();
    // PLD's emulation-mode two-byte read can cross $01ff into $0200.
    // The architectural S register still re-enters stack page $01.
    const u16 lowAddress=static_cast<u16>(s_+1u);
    const u8 lo=read8(lowAddress);
    const u8 hi=read8(static_cast<u16>(lowAddress+1u));
    s_=static_cast<u16>(0x0100u | static_cast<u8>(s_+2u));
    return static_cast<u16>(lo | (static_cast<u16>(hi)<<8));
}

u32 CPU65816::pull24Linear() {
    if (!emulationMode_) return static_cast<u32>(pull16()) | (static_cast<u32>(pull8()) << 16);
    // RTL, unlike RTS, reads the return bytes linearly when the emulation
    // stack pointer crosses $01ff. S is constrained to page $01 afterward.
    const u16 addr=static_cast<u16>(s_+1u);
    const u8 lo=read8(addr), hi=read8(static_cast<u16>(addr+1u));
    const u8 bank=read8(static_cast<u16>(addr+2u));
    s_=static_cast<u16>(0x0100u | static_cast<u8>(s_+3u));
    return static_cast<u32>(lo) | (static_cast<u32>(hi)<<8) | (static_cast<u32>(bank)<<16);
}

void CPU65816::interrupt(u16 vector, bool software) {
    waiting_ = false;
    lastInterruptVector_ = vector;
    lastInterruptSourcePc_ = (static_cast<u32>(pbr_) << 16) | pc_;
    if (!emulationMode_) push8(pbr_);
    push16(pc_);
    u8 pushed = p_;
    if (emulationMode_) {
        if (software) pushed |= 0x10u;
        else pushed &= static_cast<u8>(~0x10u);
        pushed |= 0x20u;
    }
    push8(pushed);
    setFlag(IrqDisable, true);
    setFlag(Decimal, false);
    pbr_ = 0;
    pc_ = read16(vector);
    lastInterruptTarget_ = pc_;
}

void CPU65816::branch(bool condition) {
    const s8 displacement = static_cast<s8>(fetch8());
    if (condition) {
        const u16 before = pc_;
        pc_ = static_cast<u16>(pc_ + displacement);
        branchTaken_ = true;
        branchPageCrossed_ = (before & 0xFF00u) != (pc_ & 0xFF00u);
    }
}
void CPU65816::branchLong() { const s16 displacement = static_cast<s16>(fetch16()); pc_ = static_cast<u16>(pc_ + displacement); }

void CPU65816::opOra(u16 value) {
    if (m8()) a_ = static_cast<u16>((a_ & 0xFF00u) | ((a_ | value) & 0x00FFu));
    else a_ = static_cast<u16>(a_ | value);
    setNZ(a_, m8());
}
void CPU65816::opAnd(u16 value) {
    if (m8()) a_ = static_cast<u16>((a_ & 0xFF00u) | ((a_ & value) & 0x00FFu));
    else a_ = static_cast<u16>(a_ & value);
    setNZ(a_, m8());
}
void CPU65816::opEor(u16 value) {
    if (m8()) a_ = static_cast<u16>((a_ & 0xFF00u) | ((a_ ^ value) & 0x00FFu));
    else a_ = static_cast<u16>(a_ ^ value);
    setNZ(a_, m8());
}

u16 CPU65816::decimalAdd(u16 lhs, u16 rhs, bool eightBit, bool subtract) {
    const unsigned digits = eightBit ? 2u : 4u;
    unsigned result = 0;
    int carryOrBorrow = flag(Carry) ? 0 : 1;
    if (!subtract) carryOrBorrow = flag(Carry) ? 1 : 0;
    for (unsigned digit = 0; digit < digits; ++digit) {
        const unsigned shift = digit * 4u;
        int aDigit = static_cast<int>((lhs >> shift) & 0x0Fu);
        int bDigit = static_cast<int>((rhs >> shift) & 0x0Fu);
        int value;
        if (!subtract) {
            value = aDigit + bDigit + carryOrBorrow;
            if (value >= 10) { value -= 10; carryOrBorrow = 1; }
            else carryOrBorrow = 0;
        } else {
            value = aDigit - bDigit - carryOrBorrow;
            if (value < 0) { value += 10; carryOrBorrow = 1; }
            else carryOrBorrow = 0;
        }
        result |= static_cast<unsigned>(value & 0x0F) << shift;
    }
    setFlag(Carry, subtract ? carryOrBorrow == 0 : carryOrBorrow != 0);
    return static_cast<u16>(result);
}

void CPU65816::opAdc(u16 value) {
    const bool eight = m8();
    const u32 mask = eight ? 0xFFu : 0xFFFFu;
    const u32 sign = eight ? 0x80u : 0x8000u;
    const u32 lhs = a_ & mask;
    const u32 rhs = value & mask;
    const unsigned initialCarry = flag(Carry) ? 1u : 0u;
    const u32 binary = lhs + rhs + initialCarry;
    u16 result;
    if (flag(Decimal)) result = decimalAdd(static_cast<u16>(lhs), static_cast<u16>(rhs), eight, false);
    else { setFlag(Carry, binary > mask); result = static_cast<u16>(binary & mask); }
    // 65C816 BCD overflow is determined by the high digit's intermediate
    // binary sum, *after* lower decimal carries propagate but *before* the
    // final high-digit BCD correction. Using the unadjusted 16-bit binary
    // result or the fully adjusted BCD value both fail the hardware cases.
    u32 overflowIntermediate = binary;
    if (flag(Decimal)) {
        unsigned decimalCarry = initialCarry;
        const unsigned digits = eight ? 2u : 4u;
        for (unsigned d=0;d+1u<digits;++d) {
            const unsigned shift=d*4u;
            const unsigned sum=((lhs>>shift)&15u)+((rhs>>shift)&15u)+decimalCarry;
            decimalCarry = sum >= 10u ? 1u : 0u;
        }
        const unsigned highShift=(digits-1u)*4u;
        const unsigned upper=((lhs>>highShift)&15u)+((rhs>>highShift)&15u)+decimalCarry;
        overflowIntermediate=upper << highShift;
    }
    setFlag(Overflow, ((~(lhs ^ rhs) & (lhs ^ overflowIntermediate)) & sign) != 0);
    if (eight) a_ = static_cast<u16>((a_ & 0xFF00u) | (result & 0xFFu)); else a_ = result;
    setNZ(result, eight);
}

void CPU65816::opSbc(u16 value) {
    const bool eight = m8();
    const u32 mask = eight ? 0xFFu : 0xFFFFu;
    const u32 sign = eight ? 0x80u : 0x8000u;
    const u32 lhs = a_ & mask;
    const u32 rhs = value & mask;
    const s32 binarySigned = static_cast<s32>(lhs) - static_cast<s32>(rhs) - (flag(Carry) ? 0 : 1);
    const u32 binary = static_cast<u32>(binarySigned) & mask;
    setFlag(Overflow, (((lhs ^ rhs) & (lhs ^ binary)) & sign) != 0);
    u16 result;
    if (flag(Decimal)) result = decimalAdd(static_cast<u16>(lhs), static_cast<u16>(rhs), eight, true);
    else { setFlag(Carry, binarySigned >= 0); result = static_cast<u16>(binary); }
    if (eight) a_ = static_cast<u16>((a_ & 0xFF00u) | (result & 0xFFu)); else a_ = result;
    setNZ(result, eight);
}

void CPU65816::opCmp(u16 lhs, u16 rhs, bool eightBit) {
    const u32 mask = eightBit ? 0xFFu : 0xFFFFu;
    const u32 result = (lhs & mask) - (rhs & mask);
    setFlag(Carry, (lhs & mask) >= (rhs & mask));
    setNZ(static_cast<u16>(result & mask), eightBit);
}
void CPU65816::opBit(u16 value, bool immediate) {
    const bool eight = m8();
    const u16 mask = eight ? 0x00FFu : 0xFFFFu;
    setFlag(Zero, (a_ & value & mask) == 0);
    if (!immediate) {
        setFlag(Negative, (value & (eight ? 0x0080u : 0x8000u)) != 0);
        setFlag(Overflow, (value & (eight ? 0x0040u : 0x4000u)) != 0);
    }
}
void CPU65816::opLoadA(u16 value) { if (m8()) a_ = static_cast<u16>((a_ & 0xFF00u) | (value & 0xFFu)); else a_ = value; setNZ(a_, m8()); }
void CPU65816::opLoadX(u16 value) { if (x8()) x_ = value & 0xFFu; else x_ = value; setNZ(x_, x8()); }
void CPU65816::opLoadY(u16 value) { if (x8()) y_ = value & 0xFFu; else y_ = value; setNZ(y_, x8()); }

u16 CPU65816::opAsl(u16 value, bool eight) { const u16 sign = eight ? 0x80u : 0x8000u; setFlag(Carry, (value & sign) != 0); value = static_cast<u16>((value << 1) & (eight ? 0xFFu : 0xFFFFu)); setNZ(value, eight); return value; }
u16 CPU65816::opLsr(u16 value, bool eight) { const u16 mask = eight ? 0x00FFu : 0xFFFFu; value &= mask; setFlag(Carry, (value & 1u) != 0); value = static_cast<u16>((value >> 1) & mask); setNZ(value, eight); return value; }
u16 CPU65816::opRol(u16 value, bool eight) { const u16 sign = eight ? 0x80u : 0x8000u; const bool carry = flag(Carry); setFlag(Carry, (value & sign) != 0); value = static_cast<u16>(((value << 1) | (carry ? 1u : 0u)) & (eight ? 0xFFu : 0xFFFFu)); setNZ(value, eight); return value; }
u16 CPU65816::opRor(u16 value, bool eight) { const u16 mask = eight ? 0x00FFu : 0xFFFFu; const u16 sign = eight ? 0x0080u : 0x8000u; value &= mask; const bool carry = flag(Carry); setFlag(Carry, (value & 1u) != 0); value = static_cast<u16>(((value >> 1) | (carry ? sign : 0u)) & mask); setNZ(value, eight); return value; }
u16 CPU65816::opInc(u16 value, bool eight) { value = static_cast<u16>((value + 1u) & (eight ? 0xFFu : 0xFFFFu)); setNZ(value, eight); return value; }
u16 CPU65816::opDec(u16 value, bool eight) { value = static_cast<u16>((value - 1u) & (eight ? 0xFFu : 0xFFFFu)); setNZ(value, eight); return value; }

unsigned CPU65816::step() {
    if (stopped_) { ++cycles_; return 1; }
    if (nmiPending_ && (!instructionNmiPollEnabled_ || sampledInstructionNmi_)) {
        nmiPending_ = false;
        sampledInstructionNmi_ = false;
        interrupt(emulationMode_ ? 0x00FFFAu : 0x00FFEAu, false);
        const unsigned used = emulationMode_ ? 7u : 8u;
        cycles_ += used;
        return used;
    }
    if (instructionIrqPollEnabled_ ? sampledInstructionIrq_ : (irqPending_ && !flag(IrqDisable))) {
        interrupt(emulationMode_ ? 0x00FFFEu : 0x00FFEEu, false);
        const unsigned used = emulationMode_ ? 7u : 8u;
        cycles_ += used;
        return used;
    }
    if (waiting_) { ++cycles_; return 1; }

    currentInstructionAddress_ = (static_cast<u32>(pbr_) << 16) | pc_;
    recentProgramCounters_[recentProgramCounterPosition_] = currentInstructionAddress_;
    recentProgramCounterPosition_ = (recentProgramCounterPosition_ + 1u) & 255u;
    if (recentProgramCounterCount_ < 256u) ++recentProgramCounterCount_;
    InstructionTrace& trace = instructionTrace_[instructionTracePosition_];
    trace.address = currentInstructionAddress_;
    trace.a = a_; trace.x = x_; trace.y = y_; trace.s = s_; trace.d = d_;
    trace.pbr = pbr_; trace.dbr = dbr_; trace.p = p_; trace.emulation = emulationMode_;
    const u8 opcode = fetch8();
    currentOpcode_ = opcode;
    trace.opcode = opcode;
    instructionTracePosition_ = (instructionTracePosition_ + 1u) & 511u;
    if (instructionTraceCount_ < 512u) ++instructionTraceCount_;
    ++instructions_;
    const OpcodeTiming timing = opcodeTiming(opcode);
    unsigned used = timing.baseCycles;
    u32 address;
    u16 value;
    const bool mEight = m8();
    const bool xEight = x8();
    const bool instructionEmulation = emulationMode_;
    branchTaken_ = false;
    branchPageCrossed_ = false;
    indexedPageCrossed_ = false;

#define READ_OP(method, operation) do { address = method(); operation(readM(address)); used = 4; } while (0)
#define READ_LONG_OP(method, operation) do { address = method(); operation(readMLong(address)); used = 4; } while (0)
#define WRITE_A(method) do { address = method(); writeM(address, a_); used = 4; } while (0)
#define WRITE_A_LONG(method) do { address = method(); writeMLong(address, a_); used = 4; } while (0)
#define MODIFY_M(method, operation) do { address = method(); value = readM(address); value = operation(value, m8()); writeM(address, value); used = 6; } while (0)

    switch (opcode) {
        case 0x00: fetch8(); interrupt(emulationMode_ ? 0x00FFFEu : 0x00FFE6u, true); used = 8; break;
        case 0x01: READ_OP(addrDpIndirectX, opOra); break;
        case 0x02: fetch8(); interrupt(emulationMode_ ? 0x00FFF4u : 0x00FFE4u, true); used = 8; break;
        case 0x03: READ_OP(addrStackRelative, opOra); break;
        case 0x04: address=addrDp(); value=readM(address); setFlag(Zero,(value&a_&(mEight?0xFFu:0xFFFFu))==0); writeM(address,static_cast<u16>(value|a_)); used=5; break;
        case 0x05: READ_OP(addrDp, opOra); break;
        case 0x06: MODIFY_M(addrDp, opAsl); break;
        case 0x07: READ_LONG_OP(addrDpLong, opOra); break;
        case 0x08: push8(static_cast<u8>(p_ | (emulationMode_ ? 0x30u : 0u))); used=3; break;
        case 0x09: opOra(mEight ? fetch8() : fetch16()); used=2+(mEight?0:1); break;
        case 0x0A: value=opAsl(a_,mEight); if(mEight)a_=static_cast<u16>((a_&0xFF00u)|(value&0xFFu));else a_=value; break;
        case 0x0B: push16Linear(d_); used=4; break;
        case 0x0C: address=addrAbs(); value=readM(address); setFlag(Zero,(value&a_&(mEight?0xFFu:0xFFFFu))==0); writeM(address,static_cast<u16>(value|a_)); used=6; break;
        case 0x0D: READ_OP(addrAbs, opOra); break;
        case 0x0E: MODIFY_M(addrAbs, opAsl); break;
        case 0x0F: READ_LONG_OP(addrLong, opOra); break;
        case 0x10: branch(!flag(Negative)); used=2; break;
        case 0x11: READ_OP(addrDpIndirectY, opOra); break;
        case 0x12: READ_OP(addrDpIndirect, opOra); break;
        case 0x13: READ_OP(addrStackIndirectY, opOra); break;
        case 0x14: address=addrDp(); value=readM(address); setFlag(Zero,(value&a_&(mEight?0xFFu:0xFFFFu))==0); writeM(address,static_cast<u16>(value&~a_)); used=5; break;
        case 0x15: READ_OP(addrDpX, opOra); break;
        case 0x16: MODIFY_M(addrDpX, opAsl); break;
        case 0x17: READ_LONG_OP(addrDpLongY, opOra); break;
        case 0x18: setFlag(Carry,false); break;
        case 0x19: READ_OP(addrAbsY, opOra); break;
        case 0x1A: value=opInc(a_,mEight); if(mEight)a_=static_cast<u16>((a_&0xFF00u)|(value&0xFFu));else a_=value; break;
        case 0x1B: s_=a_; if(emulationMode_)s_=static_cast<u16>(0x0100u|(s_&0xFFu)); break;
        case 0x1C: address=addrAbs(); value=readM(address); setFlag(Zero,(value&a_&(mEight?0xFFu:0xFFFFu))==0); writeM(address,static_cast<u16>(value&~a_)); used=6; break;
        case 0x1D: READ_OP(addrAbsX, opOra); break;
        case 0x1E: MODIFY_M(addrAbsX, opAsl); break;
        case 0x1F: READ_LONG_OP(addrLongX, opOra); break;

        case 0x20: value=fetch16(); push16(static_cast<u16>(pc_-1u)); pc_=value; used=6; break;
        case 0x21: READ_OP(addrDpIndirectX, opAnd); break;
        case 0x22: {
            const u32 target=fetch24();
            const u16 returnAddress=static_cast<u16>(pc_-1u);
            if (emulationMode_) {
                // JSL pushes three contiguous bytes before restoring S to
                // $01xx; its writes cross $0100 into $00ff in emulation mode.
                write8(s_,pbr_);
                write8(static_cast<u16>(s_-1u),static_cast<u8>(returnAddress>>8));
                write8(static_cast<u16>(s_-2u),static_cast<u8>(returnAddress));
                s_=static_cast<u16>(0x0100u | static_cast<u8>(s_-3u));
                if(s_<stackLowWaterMark_)stackLowWaterMark_=s_;
            } else {
                push8(pbr_);push16(returnAddress);
            }
            pbr_=static_cast<u8>(target>>16);pc_=static_cast<u16>(target);
            used=8;break;
        }
        case 0x23: READ_OP(addrStackRelative, opAnd); break;
        case 0x24: address=addrDp(); opBit(readM(address),false); used=3; break;
        case 0x25: READ_OP(addrDp, opAnd); break;
        case 0x26: MODIFY_M(addrDp, opRol); break;
        case 0x27: READ_LONG_OP(addrDpLong, opAnd); break;
        case 0x28: setP(pull8()); used=4; break;
        case 0x29: opAnd(mEight?fetch8():fetch16()); break;
        case 0x2A: value=opRol(a_,mEight); if(mEight)a_=static_cast<u16>((a_&0xFF00u)|(value&0xFFu));else a_=value; break;
        case 0x2B: d_=pull16Linear(); setNZ(d_,false); used=5; break;
        case 0x2C: address=addrAbs(); opBit(readM(address),false); used=4; break;
        case 0x2D: READ_OP(addrAbs, opAnd); break;
        case 0x2E: MODIFY_M(addrAbs, opRol); break;
        case 0x2F: READ_LONG_OP(addrLong, opAnd); break;
        case 0x30: branch(flag(Negative)); break;
        case 0x31: READ_OP(addrDpIndirectY, opAnd); break;
        case 0x32: READ_OP(addrDpIndirect, opAnd); break;
        case 0x33: READ_OP(addrStackIndirectY, opAnd); break;
        case 0x34: address=addrDpX(); opBit(readM(address),false); break;
        case 0x35: READ_OP(addrDpX, opAnd); break;
        case 0x36: MODIFY_M(addrDpX, opRol); break;
        case 0x37: READ_LONG_OP(addrDpLongY, opAnd); break;
        case 0x38: setFlag(Carry,true); break;
        case 0x39: READ_OP(addrAbsY, opAnd); break;
        case 0x3A: value=opDec(a_,mEight); if(mEight)a_=static_cast<u16>((a_&0xFF00u)|(value&0xFFu));else a_=value; break;
        case 0x3B: a_=s_; setNZ(a_,false); break;
        case 0x3C: address=addrAbsX(); opBit(readM(address),false); break;
        case 0x3D: READ_OP(addrAbsX, opAnd); break;
        case 0x3E: MODIFY_M(addrAbsX, opRol); break;
        case 0x3F: READ_LONG_OP(addrLongX, opAnd); break;

        case 0x40: setP(pull8()); pc_=pull16(); if(!emulationMode_)pbr_=pull8(); used=7; break;
        case 0x41: READ_OP(addrDpIndirectX, opEor); break;
        case 0x42: fetch8(); break;
        case 0x43: READ_OP(addrStackRelative, opEor); break;
        case 0x44: case 0x54: {
            const u8 destination=fetch8(), source=fetch8();
            dbr_=destination;
            const u32 src=(static_cast<u32>(source)<<16)|x_;
            const u32 dst=(static_cast<u32>(destination)<<16)|y_;
            write8(dst,read8(src));
            if (opcode==0x54) { ++x_; ++y_; }
            else { --x_; --y_; }
            // Block-move address registers obey X flag width on EVERY
            // iteration, including wraps, not just initial X/Y loads.
            if (x8()) { x_ &= 0x00FFu; y_ &= 0x00FFu; }
            --a_;
            if (a_!=0xFFFFu) pc_=static_cast<u16>(pc_-3u);
            used=7; break;
        }
        case 0x45: READ_OP(addrDp, opEor); break;
        case 0x46: MODIFY_M(addrDp, opLsr); break;
        case 0x47: READ_LONG_OP(addrDpLong, opEor); break;
        case 0x48: if(mEight)push8(static_cast<u8>(a_));else push16(a_); used=mEight?3:4; break;
        case 0x49: opEor(mEight?fetch8():fetch16()); break;
        case 0x4A: value=opLsr(a_,mEight); if(mEight)a_=static_cast<u16>((a_&0xFF00u)|(value&0xFFu));else a_=value; break;
        case 0x4B: push8(pbr_); used=3; break;
        case 0x4C: pc_=fetch16(); break;
        case 0x4D: READ_OP(addrAbs, opEor); break;
        case 0x4E: MODIFY_M(addrAbs, opLsr); break;
        case 0x4F: READ_LONG_OP(addrLong, opEor); break;
        case 0x50: branch(!flag(Overflow)); break;
        case 0x51: READ_OP(addrDpIndirectY, opEor); break;
        case 0x52: READ_OP(addrDpIndirect, opEor); break;
        case 0x53: READ_OP(addrStackIndirectY, opEor); break;
        case 0x55: READ_OP(addrDpX, opEor); break;
        case 0x56: MODIFY_M(addrDpX, opLsr); break;
        case 0x57: READ_LONG_OP(addrDpLongY, opEor); break;
        case 0x58: setFlag(IrqDisable,false); break;
        case 0x59: READ_OP(addrAbsY, opEor); break;
        case 0x5A: if(xEight)push8(static_cast<u8>(y_));else push16(y_); used=xEight?3:4; break;
        case 0x5B: d_=a_; setNZ(d_,false); break;
        case 0x5C: {u32 target=fetch24();pbr_=static_cast<u8>(target>>16);pc_=static_cast<u16>(target);break;}
        case 0x5D: READ_OP(addrAbsX, opEor); break;
        case 0x5E: MODIFY_M(addrAbsX, opLsr); break;
        case 0x5F: READ_LONG_OP(addrLongX, opEor); break;

        case 0x60: pc_=static_cast<u16>(pull16()+1u); used=6; break;
        case 0x61: READ_OP(addrDpIndirectX, opAdc); break;
        case 0x62: value=fetch16(); push16Linear(static_cast<u16>(pc_+static_cast<s16>(value))); used=6; break;
        case 0x63: READ_OP(addrStackRelative, opAdc); break;
        case 0x64: address=addrDp(); writeM(address,0); break;
        case 0x65: READ_OP(addrDp, opAdc); break;
        case 0x66: MODIFY_M(addrDp, opRor); break;
        case 0x67: READ_LONG_OP(addrDpLong, opAdc); break;
        case 0x68: opLoadA(mEight?pull8():pull16()); used=mEight?4:5; break;
        case 0x69: opAdc(mEight?fetch8():fetch16()); break;
        case 0x6A: value=opRor(a_,mEight); if(mEight)a_=static_cast<u16>((a_&0xFF00u)|(value&0xFFu));else a_=value; break;
        case 0x6B: {const u32 ret=pull24Linear();pc_=static_cast<u16>(static_cast<u16>(ret)+1u);pbr_=static_cast<u8>(ret>>16);used=6;break;}
        case 0x6C: {u16 pointer=fetch16();pc_=read16(pointer);break;}
        case 0x6D: READ_OP(addrAbs, opAdc); break;
        case 0x6E: MODIFY_M(addrAbs, opRor); break;
        case 0x6F: READ_LONG_OP(addrLong, opAdc); break;
        case 0x70: branch(flag(Overflow)); break;
        case 0x71: READ_OP(addrDpIndirectY, opAdc); break;
        case 0x72: READ_OP(addrDpIndirect, opAdc); break;
        case 0x73: READ_OP(addrStackIndirectY, opAdc); break;
        case 0x74: address=addrDpX();writeM(address,0);break;
        case 0x75: READ_OP(addrDpX, opAdc); break;
        case 0x76: MODIFY_M(addrDpX, opRor); break;
        case 0x77: READ_LONG_OP(addrDpLongY, opAdc); break;
        case 0x78: setFlag(IrqDisable,true); break;
        case 0x79: READ_OP(addrAbsY, opAdc); break;
        case 0x7A: opLoadY(xEight?pull8():pull16());used=xEight?4:5;break;
        case 0x7B: a_=d_;setNZ(a_,false);break;
        case 0x7C: {u16 base=fetch16();u16 pointer=static_cast<u16>(base+x_);pc_=read16((static_cast<u32>(pbr_)<<16)|pointer);break;}
        case 0x7D: READ_OP(addrAbsX, opAdc); break;
        case 0x7E: MODIFY_M(addrAbsX, opRor); break;
        case 0x7F: READ_LONG_OP(addrLongX, opAdc); break;

        case 0x80: branch(true); break;
        case 0x81: WRITE_A(addrDpIndirectX); break;
        case 0x82: branchLong(); break;
        case 0x83: WRITE_A(addrStackRelative); break;
        case 0x84: address=addrDp();if(xEight)write8(address,static_cast<u8>(y_));else write16(address,y_);break;
        case 0x85: WRITE_A(addrDp); break;
        case 0x86: address=addrDp();if(xEight)write8(address,static_cast<u8>(x_));else write16(address,x_);break;
        case 0x87: WRITE_A_LONG(addrDpLong); break;
        case 0x88: y_=static_cast<u16>((y_-1u)&(xEight?0xFFu:0xFFFFu));setNZ(y_,xEight);break;
        case 0x89: opBit(mEight?fetch8():fetch16(),true);break;
        case 0x8A: opLoadA(x_);break;
        case 0x8B: push8(dbr_);break;
        case 0x8C: address=addrAbs();if(xEight)write8(address,static_cast<u8>(y_));else {write8(address,static_cast<u8>(y_));write8((address+1u)&0xFFFFFFu,static_cast<u8>(y_>>8));}break;
        case 0x8D: WRITE_A(addrAbs); break;
        case 0x8E: address=addrAbs();if(xEight)write8(address,static_cast<u8>(x_));else {write8(address,static_cast<u8>(x_));write8((address+1u)&0xFFFFFFu,static_cast<u8>(x_>>8));}break;
        case 0x8F: WRITE_A_LONG(addrLong); break;
        case 0x90: branch(!flag(Carry));break;
        case 0x91: WRITE_A(addrDpIndirectY);break;
        case 0x92: WRITE_A(addrDpIndirect);break;
        case 0x93: WRITE_A(addrStackIndirectY);break;
        case 0x94: address=addrDpX();if(xEight)write8(address,static_cast<u8>(y_));else write16(address,y_);break;
        case 0x95: WRITE_A(addrDpX);break;
        case 0x96: address=addrDpY();if(xEight)write8(address,static_cast<u8>(x_));else write16(address,x_);break;
        case 0x97: WRITE_A_LONG(addrDpLongY);break;
        case 0x98: opLoadA(y_);break;
        case 0x99: WRITE_A(addrAbsY);break;
        case 0x9A: s_=x_;if(emulationMode_)s_=static_cast<u16>(0x0100u|(s_&0xFFu));break;
        case 0x9B: y_=x_;if(xEight)y_&=0xFFu;setNZ(y_,xEight);break; // TXY
        case 0x9C: address=addrAbs();writeM(address,0);break;
        case 0x9D: WRITE_A(addrAbsX);break;
        case 0x9E: address=addrAbsX();writeM(address,0);break;
        case 0x9F: WRITE_A_LONG(addrLongX);break;

        case 0xA0: opLoadY(xEight?fetch8():fetch16());break;
        case 0xA1: READ_OP(addrDpIndirectX, opLoadA);break;
        case 0xA2: opLoadX(xEight?fetch8():fetch16());break;
        case 0xA3: READ_OP(addrStackRelative, opLoadA);break;
        case 0xA4: address=addrDp();opLoadY(readIndex(address));break;
        case 0xA5: READ_OP(addrDp, opLoadA);break;
        case 0xA6: address=addrDp();opLoadX(readIndex(address));break;
        case 0xA7: READ_LONG_OP(addrDpLong, opLoadA);break;
        case 0xA8: opLoadY(a_);break;
        case 0xA9: opLoadA(mEight?fetch8():fetch16());break;
        case 0xAA: opLoadX(a_);break;
        case 0xAB:
            // On real hardware PLB at S=$01FF in emulation mode reads $0200,
            // while the visible 8-bit stack pointer still wraps to $0100.
            if (emulationMode_ && s_ == 0x01FFu) {
                dbr_ = read8(0x000200u);
                s_ = 0x0100u;
            } else {
                dbr_ = pull8();
            }
            setNZ(dbr_, true);
            break;
        case 0xAC: address=addrAbs();opLoadY(readIndex(address));break;
        case 0xAD: READ_OP(addrAbs, opLoadA);break;
        case 0xAE: address=addrAbs();opLoadX(readIndex(address));break;
        case 0xAF: READ_LONG_OP(addrLong, opLoadA);break;
        case 0xB0: branch(flag(Carry));break;
        case 0xB1: READ_OP(addrDpIndirectY, opLoadA);break;
        case 0xB2: READ_OP(addrDpIndirect, opLoadA);break;
        case 0xB3: READ_OP(addrStackIndirectY, opLoadA);break;
        case 0xB4: address=addrDpX();opLoadY(readIndex(address));break;
        case 0xB5: READ_OP(addrDpX, opLoadA);break;
        case 0xB6: address=addrDpY();opLoadX(readIndex(address));break;
        case 0xB7: READ_LONG_OP(addrDpLongY, opLoadA);break;
        case 0xB8: setFlag(Overflow,false);break;
        case 0xB9: READ_OP(addrAbsY, opLoadA);break;
        case 0xBA: opLoadX(s_);break;
        case 0xBB: x_=y_;if(xEight)x_&=0xFFu;setNZ(x_,xEight);break; // TYX
        case 0xBC: address=addrAbsX();opLoadY(readIndex(address));break;
        case 0xBD: READ_OP(addrAbsX, opLoadA);break;
        case 0xBE: address=addrAbsY();opLoadX(readIndex(address));break;
        case 0xBF: READ_LONG_OP(addrLongX, opLoadA);break;

        case 0xC0: opCmp(y_,xEight?fetch8():fetch16(),xEight);break;
        case 0xC1: address=addrDpIndirectX();opCmp(a_,readM(address),mEight);break;
        case 0xC2: setP(static_cast<u8>(p_ & ~fetch8()));break;
        case 0xC3: address=addrStackRelative();opCmp(a_,readM(address),mEight);break;
        case 0xC4: address=addrDp();opCmp(y_,readIndex(address),xEight);break;
        case 0xC5: address=addrDp();opCmp(a_,readM(address),mEight);break;
        case 0xC6: MODIFY_M(addrDp, opDec);break;
        case 0xC7: address=addrDpLong();opCmp(a_,readMLong(address),mEight);break;
        case 0xC8: y_=static_cast<u16>((y_+1u)&(xEight?0xFFu:0xFFFFu));setNZ(y_,xEight);break;
        case 0xC9: opCmp(a_,mEight?fetch8():fetch16(),mEight);break;
        case 0xCA: x_=static_cast<u16>((x_-1u)&(xEight?0xFFu:0xFFFFu));setNZ(x_,xEight);break;
        case 0xCB: waiting_=true;break;
        case 0xCC: address=addrAbs();opCmp(y_,readIndex(address),xEight);break;
        case 0xCD: address=addrAbs();opCmp(a_,readM(address),mEight);break;
        case 0xCE: MODIFY_M(addrAbs, opDec);break;
        case 0xCF: address=addrLong();opCmp(a_,readMLong(address),mEight);break;
        case 0xD0: branch(!flag(Zero));break;
        case 0xD1: address=addrDpIndirectY();opCmp(a_,readM(address),mEight);break;
        case 0xD2: address=addrDpIndirect();opCmp(a_,readM(address),mEight);break;
        case 0xD3: address=addrStackIndirectY();opCmp(a_,readM(address),mEight);break;
        case 0xD4: {u16 pointer=static_cast<u16>(d_+fetch8());push16Linear(read16(pointer));used=6;break;}
        case 0xD5: address=addrDpX();opCmp(a_,readM(address),mEight);break;
        case 0xD6: MODIFY_M(addrDpX, opDec);break;
        case 0xD7: address=addrDpLongY();opCmp(a_,readMLong(address),mEight);break;
        case 0xD8: setFlag(Decimal,false);break;
        case 0xD9: address=addrAbsY();opCmp(a_,readM(address),mEight);break;
        case 0xDA: if(xEight)push8(static_cast<u8>(x_));else push16(x_);break;
        case 0xDB: stopped_=true;break;
        case 0xDC: {u16 pointer=fetch16();u32 target=read24(pointer);pbr_=static_cast<u8>(target>>16);pc_=static_cast<u16>(target);break;}
        case 0xDD: address=addrAbsX();opCmp(a_,readM(address),mEight);break;
        case 0xDE: MODIFY_M(addrAbsX, opDec);break;
        case 0xDF: address=addrLongX();opCmp(a_,readMLong(address),mEight);break;

        case 0xE0: opCmp(x_,xEight?fetch8():fetch16(),xEight);break;
        case 0xE1: READ_OP(addrDpIndirectX, opSbc);break;
        case 0xE2: setP(static_cast<u8>(p_ | fetch8()));break;
        case 0xE3: READ_OP(addrStackRelative, opSbc);break;
        case 0xE4: address=addrDp();opCmp(x_,readIndex(address),xEight);break;
        case 0xE5: READ_OP(addrDp, opSbc);break;
        case 0xE6: MODIFY_M(addrDp, opInc);break;
        case 0xE7: READ_LONG_OP(addrDpLong, opSbc);break;
        case 0xE8: x_=static_cast<u16>((x_+1u)&(xEight?0xFFu:0xFFFFu));setNZ(x_,xEight);break;
        case 0xE9: opSbc(mEight?fetch8():fetch16());break;
        case 0xEA: break;
        case 0xEB: a_=static_cast<u16>((a_>>8)|(a_<<8));setNZ(a_&0xFFu,true);break;
        case 0xEC: address=addrAbs();opCmp(x_,readIndex(address),xEight);break;
        case 0xED: READ_OP(addrAbs, opSbc);break;
        case 0xEE: MODIFY_M(addrAbs, opInc);break;
        case 0xEF: READ_LONG_OP(addrLong, opSbc);break;
        case 0xF0: branch(flag(Zero));break;
        case 0xF1: READ_OP(addrDpIndirectY, opSbc);break;
        case 0xF2: READ_OP(addrDpIndirect, opSbc);break;
        case 0xF3: READ_OP(addrStackIndirectY, opSbc);break;
        case 0xF4: push16Linear(fetch16());break;
        case 0xF5: READ_OP(addrDpX, opSbc);break;
        case 0xF6: MODIFY_M(addrDpX, opInc);break;
        case 0xF7: READ_LONG_OP(addrDpLongY, opSbc);break;
        case 0xF8: setFlag(Decimal,true);break;
        case 0xF9: READ_OP(addrAbsY, opSbc);break;
        case 0xFA: opLoadX(xEight?pull8():pull16());break;
        case 0xFB: { const bool oldCarry=flag(Carry); setFlag(Carry,emulationMode_); emulationMode_=oldCarry; if(emulationMode_){p_|=static_cast<u8>(Accumulator8|Index8);s_=static_cast<u16>(0x0100u|(s_&0xFFu));x_&=0xFFu;y_&=0xFFu;} break; }
        case 0xFC: {
            const u16 base=fetch16();
            const u16 pointer=static_cast<u16>(base+x_);
            const u16 ret=static_cast<u16>(pc_-1u);
            push16Linear(ret);
            pc_=read16((static_cast<u32>(pbr_)<<16)|pointer);
            break;
        }
        case 0xFD: READ_OP(addrAbsX, opSbc);break;
        case 0xFE: MODIFY_M(addrAbsX, opInc);break;
        case 0xFF: READ_LONG_OP(addrLongX, opSbc);break;
        default: break;
    }

#undef READ_OP
#undef READ_LONG_OP
#undef WRITE_A
#undef WRITE_A_LONG
#undef MODIFY_M

    // Recalculate from structured metadata so the old per-case assignments no
    // longer act as a second, inconsistent timing table. The state flags are
    // captured before the instruction because REP/SEP/XCE may change them.
    used = timing.baseCycles;
    if ((timing.flags & TimingAccumulatorWidth) && !mEight) ++used;
    if ((timing.flags & TimingIndexWidth) && !xEight) ++used;
    if ((timing.flags & TimingDirectPage) && (d_ & 0x00FFu) != 0u) ++used;
    if (timing.flags & TimingIndexed) {
        const bool writeHasBasePenalty = (timing.flags & TimingIndexedWrite) != 0;
        if (!xEight || (!writeHasBasePenalty && indexedPageCrossed_)) ++used;
    }
    if ((timing.flags & TimingBranch) && branchTaken_) {
        ++used;
        if (instructionEmulation && branchPageCrossed_) ++used;
    }
    if ((timing.flags & TimingNativeInterrupt) && !instructionEmulation) ++used;
    if ((timing.flags & TimingNativeRti) && instructionEmulation && used != 0u) --used;

    cycles_ += used;
    return used;
}

void CPU65816::requestNmi() { nmiPending_ = true; waiting_ = false; }
void CPU65816::setInstructionIrqPollEnabled(bool enabled) { instructionIrqPollEnabled_ = enabled; }
void CPU65816::latchInstructionIrqPoll(bool asserted) { sampledInstructionIrq_ = asserted && !flag(IrqDisable); }
bool CPU65816::latchedInstructionIrqPending() const { return sampledInstructionIrq_; }
void CPU65816::setInstructionNmiPollEnabled(bool enabled) { instructionNmiPollEnabled_ = enabled; }
void CPU65816::latchInstructionNmiPoll(bool asserted) { sampledInstructionNmi_ = asserted; }
bool CPU65816::latchedInstructionNmiPending() const { return sampledInstructionNmi_; }
void CPU65816::requestIrq() { setIrqLine(true); }
void CPU65816::setIrqLine(bool asserted) {
    irqPending_ = asserted;
    if (asserted) waiting_ = false;
}
u16 CPU65816::a() const { return a_; }
u16 CPU65816::x() const { return x_; }
u16 CPU65816::y() const { return y_; }
u16 CPU65816::s() const { return s_; }
u16 CPU65816::d() const { return d_; }
u16 CPU65816::pc() const { return pc_; }
u8 CPU65816::pbr() const { return pbr_; }
u8 CPU65816::dbr() const { return dbr_; }
u8 CPU65816::p() const { return p_; }
u8 CPU65816::lastExecutedOpcode() const { return currentOpcode_; }
bool CPU65816::emulationMode() const { return emulationMode_; }
bool CPU65816::stopped() const { return stopped_; }
bool CPU65816::waiting() const { return waiting_; }
bool CPU65816::irqLineAsserted() const { return irqPending_; }
bool CPU65816::nmiPending() const { return nmiPending_; }
u32 CPU65816::dominantRecentProgramCounter(unsigned& hits) const {
    hits = 0;
    u32 dominant = 0;
    for (unsigned i = 0; i < recentProgramCounterCount_; ++i) {
        const u32 candidate = recentProgramCounters_[i];
        unsigned count = 0;
        for (unsigned j = 0; j < recentProgramCounterCount_; ++j)
            if (recentProgramCounters_[j] == candidate) ++count;
        if (count > hits) { hits = count; dominant = candidate; }
    }
    return dominant;
}
unsigned CPU65816::recentInstructionTraceCount() const { return instructionTraceCount_; }
const CPU65816::InstructionTrace& CPU65816::recentInstructionTrace(unsigned oldestIndex) const {
    static const InstructionTrace empty;
    if (oldestIndex >= instructionTraceCount_) return empty;
    const unsigned oldest = (instructionTracePosition_ + 512u - instructionTraceCount_) & 511u;
    return instructionTrace_[(oldest + oldestIndex) & 511u];
}
u16 CPU65816::stackLowWaterMark() const { return stackLowWaterMark_; }
bool CPU65816::stackPageWasOverwritten() const { return stackPageWasOverwritten_; }
u32 CPU65816::lastStackPageWritePc() const { return lastStackPageWritePc_; }
u32 CPU65816::lastStackPageWriteAddress() const { return lastStackPageWriteAddress_; }
u8 CPU65816::lastStackPageWriteValue() const { return lastStackPageWriteValue_; }
u8 CPU65816::lastStackPageWriteOpcode() const { return lastStackPageWriteOpcode_; }
u32 CPU65816::lastGameModeWritePc() const { return lastGameModeWritePc_; }
u8 CPU65816::lastGameModeWriteValue() const { return lastGameModeWriteValue_; }
u8 CPU65816::lastGameModeWriteOpcode() const { return lastGameModeWriteOpcode_; }
u16 CPU65816::lastInterruptVector() const { return lastInterruptVector_; }
u32 CPU65816::lastInterruptSourcePc() const { return lastInterruptSourcePc_; }
u16 CPU65816::lastInterruptTarget() const { return lastInterruptTarget_; }
u64 CPU65816::cycles() const { return cycles_; }
u64 CPU65816::instructions() const { return instructions_; }

} // namespace snes
