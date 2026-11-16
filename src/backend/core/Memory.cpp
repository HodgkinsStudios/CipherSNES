#include "snes/core/Memory.hpp"
#include <algorithm>

namespace snes {

Memory::Memory() : wram_(128u * KiB, 0) {}
void Memory::reset() { std::fill(wram_.begin(), wram_.end(), 0); }

u8 Memory::readWRAM(u32 address) const {
    return wram_[address & 0x1FFFFu];
}

void Memory::writeWRAM(u32 address, u8 value) {
    wram_[address & 0x1FFFFu] = value;
}

const std::vector<u8>& Memory::wram() const { return wram_; }

} // namespace snes
