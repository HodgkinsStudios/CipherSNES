#ifndef SNES_CORE_DEBUGGER_HPP
#define SNES_CORE_DEBUGGER_HPP

#include <string>

namespace snes {

class System;

class Debugger {
public:
    Debugger();
    void attach(System* system);
    std::string cpuSummary() const;

private:
    System* system_;
};

} // namespace snes

#endif
