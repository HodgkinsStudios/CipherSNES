#ifndef SNES_CORE_OPCODETIMING_HPP
#define SNES_CORE_OPCODETIMING_HPP

#include "snes/Types.hpp"

namespace snes {

enum OpcodeTimingFlag {
    TimingNone             = 0,
    TimingAccumulatorWidth = 1u << 0,
    TimingIndexWidth       = 1u << 1,
    TimingDirectPage       = 1u << 2,
    TimingIndexed          = 1u << 3,
    TimingIndexedWrite     = 1u << 4,
    TimingBranch           = 1u << 5,
    TimingNativeInterrupt  = 1u << 6,
    TimingNativeRti        = 1u << 7
};

struct OpcodeTiming {
    u8 baseCycles;
    u8 baseBytes;
    u16 flags;
};

// Table 5-4 of the WDC W65C816S data sheet is the source of truth for the
// 8-bit, aligned, non-crossing base values. Dynamic penalties are represented
// as flags rather than being scattered through the opcode implementation.
inline OpcodeTiming opcodeTiming(u8 opcode) {
    static const u8 cycles[256] = {
        7,6,7,4,5,3,5,6,3,2,2,4,6,4,6,5,
        2,5,5,7,5,4,6,6,2,4,2,2,6,4,7,5,
        6,6,8,4,3,3,5,6,4,2,2,5,4,4,6,5,
        2,5,5,7,4,4,6,6,2,4,2,2,4,4,7,5,
        7,6,2,4,7,3,5,6,3,2,2,3,3,4,6,5,
        2,5,5,7,7,4,6,6,2,4,3,2,4,4,7,5,
        6,6,6,4,3,3,5,6,4,2,2,6,5,4,6,5,
        2,5,5,7,4,4,6,6,2,4,4,2,6,4,7,5,
        2,6,4,4,3,3,3,6,2,2,2,3,4,4,4,5,
        2,6,5,7,4,4,4,6,2,5,2,2,4,5,5,5,
        2,6,2,4,3,3,3,6,2,2,2,4,4,4,4,5,
        2,5,5,7,4,4,4,6,2,4,2,2,4,4,4,5,
        2,6,3,4,3,3,5,6,2,2,2,3,4,4,6,5,
        2,5,5,7,6,4,6,6,2,4,3,3,6,4,7,5,
        2,6,3,4,3,3,5,6,2,2,2,3,4,4,6,5,
        2,5,5,7,5,4,6,6,2,4,4,2,8,4,7,5
    };
    static const u8 bytes[256] = {
        2,2,2,2,2,2,2,2,1,2,1,1,3,3,3,4,
        2,2,2,2,2,2,2,2,1,3,1,1,3,3,3,4,
        3,2,4,2,2,2,2,2,1,2,1,1,3,3,3,4,
        2,2,2,2,2,2,2,2,1,3,1,1,3,3,3,4,
        1,2,2,2,3,2,2,2,1,2,1,1,3,3,3,4,
        2,2,2,2,3,2,2,2,1,3,1,1,4,3,3,4,
        1,2,3,2,2,2,2,2,1,2,1,1,3,3,3,4,
        2,2,2,2,2,2,2,2,1,3,1,1,3,3,3,4,
        2,2,3,2,2,2,2,2,1,2,1,1,3,3,3,4,
        2,2,2,2,2,2,2,2,1,3,1,1,3,3,3,4,
        2,2,2,2,2,2,2,2,1,2,1,1,3,3,3,4,
        2,2,2,2,2,2,2,2,1,3,1,1,3,3,3,4,
        2,2,2,2,2,2,2,2,1,2,1,1,3,3,3,4,
        2,2,2,2,2,2,2,2,1,3,1,1,3,3,3,4,
        2,2,2,2,2,2,2,2,1,2,1,1,3,3,3,4,
        2,2,2,2,3,2,2,2,1,3,1,1,3,3,3,4
    };

    u16 flags = TimingNone;
    switch (opcode) {
        case 0x04: case 0x06: case 0x0C: case 0x0E:
        case 0x14: case 0x16: case 0x1C: case 0x1E:
        case 0x24: case 0x26: case 0x2C: case 0x2E:
        case 0x34: case 0x36: case 0x3C: case 0x3E:
        case 0x46: case 0x48: case 0x4E: case 0x56: case 0x5E:
        case 0x64: case 0x66: case 0x68: case 0x6E: case 0x74: case 0x76: case 0x7E:
        case 0x89: case 0x9C: case 0x9E:
        case 0xC6: case 0xCE: case 0xD6: case 0xDE:
        case 0xE6: case 0xEE: case 0xF6: case 0xFE:
            flags |= TimingAccumulatorWidth; break;
        default: break;
    }
    switch (opcode & 0xE0u) {
        case 0x00u: case 0x20u: case 0x40u: case 0x60u:
        case 0xA0u: case 0xC0u: case 0xE0u: {
            const u8 low = static_cast<u8>(opcode & 0x1Fu);
            if (low == 0x01u || low == 0x03u || low == 0x05u || low == 0x07u ||
                low == 0x09u || low == 0x0Du || low == 0x0Fu || low == 0x11u ||
                low == 0x12u || low == 0x13u || low == 0x15u || low == 0x17u ||
                low == 0x19u || low == 0x1Du || low == 0x1Fu)
                flags |= TimingAccumulatorWidth;
            break;
        }
        default: break;
    }
    switch (opcode) {
        case 0x81: case 0x83: case 0x85: case 0x87: case 0x8D: case 0x8F:
        case 0x91: case 0x92: case 0x93: case 0x95: case 0x97: case 0x99:
        case 0x9D: case 0x9F:
            flags |= TimingAccumulatorWidth; break;
        default: break;
    }
    switch (opcode) {
        case 0x5A: case 0x7A: case 0x84: case 0x86: case 0x8C: case 0x8E:
        case 0x94: case 0x96: case 0xA0: case 0xA2: case 0xA4: case 0xA6:
        case 0xAC: case 0xAE: case 0xB4: case 0xB6: case 0xBC: case 0xBE:
        case 0xC0: case 0xC4: case 0xCC: case 0xDA: case 0xE0: case 0xE4:
        case 0xEC: case 0xFA:
            flags |= TimingIndexWidth; break;
        default: break;
    }
    switch (opcode) {
        case 0x01: case 0x04: case 0x05: case 0x06: case 0x07:
        case 0x11: case 0x12: case 0x14: case 0x15: case 0x16: case 0x17:
        case 0x21: case 0x24: case 0x25: case 0x26: case 0x27:
        case 0x31: case 0x32: case 0x34: case 0x35: case 0x36: case 0x37:
        case 0x41: case 0x45: case 0x46: case 0x47:
        case 0x51: case 0x52: case 0x55: case 0x56: case 0x57:
        case 0x61: case 0x64: case 0x65: case 0x66: case 0x67:
        case 0x71: case 0x72: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x81: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x91: case 0x92: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0xA1: case 0xA4: case 0xA5: case 0xA6: case 0xA7:
        case 0xB1: case 0xB2: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        case 0xC1: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
        case 0xD1: case 0xD2: case 0xD4: case 0xD5: case 0xD6: case 0xD7:
        case 0xE1: case 0xE4: case 0xE5: case 0xE6: case 0xE7:
        case 0xF1: case 0xF2: case 0xF5: case 0xF6: case 0xF7:
            flags |= TimingDirectPage; break;
        default: break;
    }
    switch (opcode) {
        case 0x11: case 0x19: case 0x1D: case 0x1E:
        case 0x31: case 0x39: case 0x3C: case 0x3D: case 0x3E:
        case 0x51: case 0x59: case 0x5D: case 0x5E:
        case 0x71: case 0x79: case 0x7D: case 0x7E:
        case 0x91: case 0x99: case 0x9D: case 0x9E:
        case 0xB1: case 0xB9: case 0xBC: case 0xBD: case 0xBE:
        case 0xD1: case 0xD9: case 0xDD: case 0xDE:
        case 0xF1: case 0xF9: case 0xFD: case 0xFE:
            flags |= TimingIndexed; break;
        default: break;
    }
    switch (opcode) {
        case 0x1E: case 0x3E: case 0x5E: case 0x7E:
        case 0x91: case 0x99: case 0x9D: case 0x9E:
        case 0xDE: case 0xFE:
            flags |= TimingIndexedWrite; break;
        default: break;
    }
    switch (opcode) {
        case 0x10: case 0x30: case 0x50: case 0x70: case 0x80:
        case 0x90: case 0xB0: case 0xD0: case 0xF0:
            flags |= TimingBranch; break;
        default: break;
    }
    if (opcode == 0x00u || opcode == 0x02u) flags |= TimingNativeInterrupt;
    if (opcode == 0x40u) flags |= TimingNativeRti;
    OpcodeTiming result = { cycles[opcode], bytes[opcode], flags };
    return result;
}

} // namespace snes

#endif
