#ifndef SNES_CORE_COMPONENT_HPP
#define SNES_CORE_COMPONENT_HPP

namespace snes {

class Component {
public:
    virtual ~Component() {}
    virtual void reset() = 0;
};

} // namespace snes

#endif
