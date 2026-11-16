#ifndef SNES_CORE_SYSTEM_HPP
#define SNES_CORE_SYSTEM_HPP

#include <string>
#include "snes/core/APU.hpp"
#include "snes/core/Bus.hpp"
#include "snes/core/CPU65816.hpp"
#include "snes/core/Cartridge.hpp"
#include "snes/core/Controller.hpp"
#include "snes/core/DMA.hpp"
#include "snes/core/Debugger.hpp"
#include "snes/core/Memory.hpp"
#include "snes/core/PPU.hpp"
#include "snes/core/SaveState.hpp"
#include "snes/core/Scheduler.hpp"

namespace snes {

class System {
    friend class SaveStateManager;
public:
    enum RunState {
        Empty,
        LoadedStopped,
        Running,
        Paused,
        Faulted
    };

    System();
    ~System();
    bool loadCartridge(const std::string& path, std::string& error,
                       bool allowExperimentalChips = false,
                       const std::string& savePathBase = std::string());
    void unloadCartridge();
    void reset();
    bool stepInstruction();
    bool runFrame(bool renderVideo = true, unsigned instructionBudget = 1000000u);
    void setPaused(bool paused);
    void setControllerButton(unsigned controller, Controller::Button button, bool pressed);
    void setControllerDevice(unsigned controller, Controller::Device device);
    void setMouseButton(unsigned controller, Controller::MouseButton button, bool pressed);
    void moveMouse(unsigned controller, int deltaX, int deltaY);
    void setLightGunButton(unsigned controller, Controller::LightGunButton button, bool pressed);
    void setLightGunAim(unsigned controller, unsigned gun, int x, int y, bool offscreen = false);
    void setSuperScopeTurbo(unsigned controller, bool enabled);
    void enableMultitapPort2(bool enabled);
    bool saveBatteryRam(std::string& error) const;
    bool saveState(const std::string& path, std::string& error) const;
    bool loadState(const std::string& path, std::string& error);
    bool saveStateToMemory(std::vector<u8>& output, std::string& error) const;
    bool loadStateFromMemory(const std::vector<u8>& input, std::string& error);
    bool saveTransientStateToMemory(std::vector<u8>& output, std::string& error) const;
    bool loadTransientStateFromMemory(const std::vector<u8>& input, std::string& error);

    RunState state() const;
    const Cartridge& cartridge() const;
    Cartridge& cartridge();
    const CPU65816& cpu() const;
    CPU65816& cpu();
    const PPU& ppu() const;
    PPU& ppu();
    const APU& apu() const;
    APU& apu();
    const Scheduler& scheduler() const;
    const Memory& memory() const;
    Memory& memory();
    const Bus& bus() const;
    Bus& bus();
    const DMA& dma() const;
    const Controller& controller(unsigned index) const;
    const std::vector<s16>& pendingAudioSamples() const;
    void clearPendingAudioSamples();

private:
    Memory memory_;
    Cartridge cartridge_;
    PPU ppu_;
    APU apu_;
    DMA dma_;
    Controller controller1_;
    Controller controller2_;
    Controller controller3_;
    Controller controller4_;
    Controller controller5_;
    Bus bus_;
    CPU65816 cpu_;
    Scheduler scheduler_;
    SaveStateManager saveStates_;
    Debugger debugger_;
    RunState state_;
};

} // namespace snes

#endif
