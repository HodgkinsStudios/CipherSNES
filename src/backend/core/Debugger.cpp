#include "snes/core/Debugger.hpp"
#include "snes/core/System.hpp"
#include <iomanip>
#include <sstream>

namespace snes {

Debugger::Debugger() : system_(0) {}
void Debugger::attach(System* system) { system_ = system; }

std::string Debugger::cpuSummary() const {
    if (!system_) return "Debugger detached";
    std::ostringstream out;
    out << "PBR:PC=" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(2) << static_cast<unsigned>(system_->cpu().pbr()) << ':'
        << std::setw(4) << static_cast<unsigned>(system_->cpu().pc())
        << " cycles=" << std::dec << system_->cpu().cycles();
    return out.str();
}

} // namespace snes
