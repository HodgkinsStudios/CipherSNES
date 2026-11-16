#ifndef SNES_CORE_CPU65816_BUS_HPP
#define SNES_CORE_CPU65816_BUS_HPP

#include "snes/Types.hpp"

namespace snes {

// Minimal address bus used by the 65C816 execution core.  Keeping the CPU
// independent from the console Bus lets the same verified instruction engine
// drive both the S-CPU and the SA-1's embedded 65C816.
class CPU65816Bus {
public:
    virtual ~CPU65816Bus() {}
    virtual u8 read(u32 address) = 0;
    virtual void write(u32 address, u8 value) = 0;
};

} // namespace snes

#endif
