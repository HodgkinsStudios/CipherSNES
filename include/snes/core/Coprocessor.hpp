#ifndef SNES_CORE_COPROCESSOR_HPP
#define SNES_CORE_COPROCESSOR_HPP

#include "snes/Types.hpp"
#include "snes/core/Component.hpp"

namespace snes {

// Common future interface for Super FX, SA-1, DSP-1/2/3/4, CX4, S-DD1,
// SPC7110, ST010/011/018, and other cartridge enhancement hardware.
class Coprocessor : public Component {
public:
    virtual ~Coprocessor() {}
    virtual const char* name() const = 0;
    virtual bool handlesAddress(u32 address) const = 0;
    virtual u8 read(u32 address) = 0;
    virtual void write(u32 address, u8 value) = 0;
    virtual void step(u64 masterCycles) = 0;
};

} // namespace snes

#endif
