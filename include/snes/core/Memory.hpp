#ifndef SNES_CORE_MEMORY_HPP
#define SNES_CORE_MEMORY_HPP

#include <vector>
#include "snes/Types.hpp"
#include "snes/core/Component.hpp"

namespace snes {

class Memory : public Component {
    friend class SaveStateManager;
public:
    Memory();
    void reset();

    u8 readWRAM(u32 address) const;
    void writeWRAM(u32 address, u8 value);
    const std::vector<u8>& wram() const;

private:
    std::vector<u8> wram_;
};

} // namespace snes

#endif
