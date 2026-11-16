#include "snes/core/System.hpp"
#include <utility>

namespace snes {

System::System() : state_(Empty) {
    bus_.connect(&memory_, &cartridge_, &ppu_, &apu_, &dma_, &controller1_, &controller2_,
                 &controller3_, &controller4_, &controller5_);
    cpu_.connectBus(&bus_);
    scheduler_.connect(&cpu_, &ppu_, &apu_, &dma_, &bus_);
    debugger_.attach(this);
    reset();
    state_ = Empty;
}

System::~System() {
    std::string ignored;
    saveBatteryRam(ignored);
}

bool System::loadCartridge(const std::string& path, std::string& error,
                           bool allowExperimentalChips, const std::string& savePathBase) {
    // Validate and fully prepare the replacement before touching the active
    // cartridge.  A failed load therefore cannot leave a previously running
    // game attached to an empty or half-parsed cartridge.
    Cartridge replacement;
    if (!replacement.loadFromFile(path, error, savePathBase)) return false;
    const Cartridge::Capability capability = replacement.enhancementCapability();
    if (capability == Cartridge::CapabilityUnsupported) {
        error = std::string("This ROM requires unsupported cartridge hardware: ") +
                replacement.enhancementName() + ". " +
                replacement.enhancementImplementationStatus();
        return false;
    }
    if (capability == Cartridge::CapabilityExperimentalBootOnly &&
        !allowExperimentalChips) {
        error = std::string("This ROM requires ") + replacement.enhancementName() +
                ", currently classified ExperimentalBootOnly. Re-run with "
                "--allow-experimental-chips to exercise this timing-sensitive, "
                "commercial-ROM-unvalidated processor path. " +
                replacement.enhancementImplementationStatus();
        return false;
    }
    if (cartridge_.loaded()) {
        std::string ignored;
        cartridge_.saveRam(ignored);
    }
    cartridge_ = std::move(replacement);
    // A newly loaded cartridge starts a new powered-on session. Previous
    // cartridge WRAM must not leak across game loads.
    memory_.reset();
    reset();
    state_ = Running;
    return true;
}

void System::unloadCartridge() {
    std::string ignored;
    cartridge_.saveRam(ignored);
    cartridge_.unload();
    // Empty machine / next cartridge starts with fresh WRAM.
    memory_.reset();
    reset();
    state_ = Empty;
}

void System::reset() {
    // A console RESET is not a power cycle: physical WRAM survives the
    // reset signal.  Only a power-on/new-cartridge transition clears it.
    // In particular the CPUMSC reset/STP test needs real WRAM persistence.
    ppu_.setPal(cartridge_.loaded() && cartridge_.isPal());
    ppu_.reset();
    apu_.reset();
    dma_.reset();
    controller1_.reset();
    controller2_.reset();
    controller3_.reset();
    controller4_.reset();
    controller5_.reset();
    bus_.reset();
    cartridge_.reset();
    scheduler_.reset();
    cpu_.reset();
    state_ = cartridge_.loaded() ? Running : Empty;
}

bool System::runFrame(bool renderVideo, unsigned instructionBudget) {
    if (state_ != Running) return false;
    if (!scheduler_.runFrame(instructionBudget, renderVideo)) {
        state_ = Faulted;
        return false;
    }
    return true;
}

bool System::stepInstruction() {
    if (state_ != Running) return false;
    if (!scheduler_.stepInstruction()) {
        state_ = Faulted;
        return false;
    }
    return true;
}

void System::setPaused(bool paused) {
    if (!cartridge_.loaded()) return;
    state_ = paused ? Paused : Running;
}

void System::setControllerButton(unsigned controller, Controller::Button button, bool pressed) {
    if (controller < 5u) {
        Controller* controllers[] = { &controller1_, &controller2_, &controller3_, &controller4_, &controller5_ };
        controllers[controller]->setButton(button, pressed);
    }
}

void System::setControllerDevice(unsigned controller, Controller::Device device) {
    if (controller < 5u) {
        Controller* controllers[] = { &controller1_, &controller2_, &controller3_, &controller4_, &controller5_ };
        controllers[controller]->setDevice(device);
    }
}

void System::setMouseButton(unsigned controller, Controller::MouseButton button, bool pressed) {
    if (controller < 5u) {
        Controller* controllers[] = { &controller1_, &controller2_, &controller3_, &controller4_, &controller5_ };
        controllers[controller]->setMouseButton(button, pressed);
    }
}

void System::moveMouse(unsigned controller, int deltaX, int deltaY) {
    if (controller < 5u) {
        Controller* controllers[] = { &controller1_, &controller2_, &controller3_, &controller4_, &controller5_ };
        controllers[controller]->moveMouse(deltaX, deltaY);
    }
}

void System::setLightGunButton(unsigned controller, Controller::LightGunButton button, bool pressed) {
    if (controller < 5u) {
        Controller* controllers[] = { &controller1_, &controller2_, &controller3_, &controller4_, &controller5_ };
        controllers[controller]->setLightGunButton(button, pressed);
    }
}

void System::setLightGunAim(unsigned controller, unsigned gun, int x, int y, bool offscreen) {
    if (controller < 5u) {
        Controller* controllers[] = { &controller1_, &controller2_, &controller3_, &controller4_, &controller5_ };
        controllers[controller]->setLightGunAim(gun, x, y, offscreen);
    }
}

void System::setSuperScopeTurbo(unsigned controller, bool enabled) {
    if (controller < 5u) {
        Controller* controllers[] = { &controller1_, &controller2_, &controller3_, &controller4_, &controller5_ };
        controllers[controller]->setSuperScopeTurbo(enabled);
    }
}

void System::enableMultitapPort2(bool enabled) { bus_.setMultitapPort2(enabled); }

bool System::saveBatteryRam(std::string& error) const { return cartridge_.saveRam(error); }
bool System::saveState(const std::string& path, std::string& error) const { return saveStates_.save(*this, path, error); }
bool System::loadState(const std::string& path, std::string& error) { return saveStates_.load(*this, path, error); }
bool System::saveStateToMemory(std::vector<u8>& output, std::string& error) const { return saveStates_.saveToMemory(*this, output, error); }
bool System::loadStateFromMemory(const std::vector<u8>& input, std::string& error) { return saveStates_.loadFromMemory(*this, input, error); }
bool System::saveTransientStateToMemory(std::vector<u8>& output, std::string& error) const { return saveStates_.saveTransientToMemory(*this, output, error); }
bool System::loadTransientStateFromMemory(const std::vector<u8>& input, std::string& error) { return saveStates_.loadTransientFromMemory(*this, input, error); }

System::RunState System::state() const { return state_; }
const Cartridge& System::cartridge() const { return cartridge_; }
Cartridge& System::cartridge() { return cartridge_; }
const CPU65816& System::cpu() const { return cpu_; }
CPU65816& System::cpu() { return cpu_; }
const PPU& System::ppu() const { return ppu_; }
PPU& System::ppu() { return ppu_; }
const APU& System::apu() const { return apu_; }
APU& System::apu() { return apu_; }
const Scheduler& System::scheduler() const { return scheduler_; }
const Memory& System::memory() const { return memory_; }
Memory& System::memory() { return memory_; }
const Bus& System::bus() const { return bus_; }
Bus& System::bus() { return bus_; }
const DMA& System::dma() const { return dma_; }
const Controller& System::controller(unsigned index) const {
    switch (index) {
        case 0: return controller1_;
        case 1: return controller2_;
        case 2: return controller3_;
        case 3: return controller4_;
        default: return controller5_;
    }
}
const std::vector<s16>& System::pendingAudioSamples() const { return apu_.pendingSamples(); }
void System::clearPendingAudioSamples() { apu_.clearPendingSamples(); }

} // namespace snes
