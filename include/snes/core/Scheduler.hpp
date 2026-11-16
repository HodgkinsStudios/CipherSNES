#ifndef SNES_CORE_SCHEDULER_HPP
#define SNES_CORE_SCHEDULER_HPP

#include "snes/Types.hpp"
#include "snes/core/Component.hpp"
#include <functional>

namespace snes {

class CPU65816;
class PPU;
class APU;
class DMA;
class Bus;

// CP51: optional read-only timing events, disabled during ordinary emulation.
// The probe observes completed bus slots, scheduler arbitration and MMIO.
// It does not advance the machine or alter save-state serialization.
struct TimingEvent {
    const char* kind;
    u64 masterCycle;
    int scanline;
    int horizontalClock;
    u32 programCounter;
    u32 address;
    u32 value;
};

class Scheduler : public Component {
    friend class SaveStateManager;
    friend class Bus;
public:
    Scheduler();
    void connect(CPU65816* cpu, PPU* ppu, APU* apu, DMA* dma, Bus* bus);
    void reset();
    bool stepInstruction();
    void pollIrqAtCpuRegisterWrite();
    void noteCpuDmaWrite();
    bool runFrame(unsigned instructionBudget = 1000000u, bool renderVideo = true);
    u64 masterCycles() const;
    u64 framesRun() const;
    using TimingCallback = std::function<void(const TimingEvent&)>;
    void setTimingCallback(TimingCallback callback);
    void emitTimingEvent(const char* kind, u32 address = 0, u32 value = 0) const;

private:
    void advanceHardware(unsigned masterClocks);
    void advanceBusAccess(unsigned masterClocks);
    void processHardwareEvents();
    TimingCallback timingCallback_;
    CPU65816* cpu_;
    PPU* ppu_;
    APU* apu_;
    DMA* dma_;
    Bus* bus_;
    u64 masterCycles_;
    u64 framesRun_;
    unsigned ppuRemainder_;
    unsigned apuRemainder_;
    bool processingEvents_;
    bool irqPolledOnWrite_;
    bool nmiBusPollSample_;
    bool cpuDmaWriteDuringInstruction_;
    bool postDmaInterruptInhibit_;
    u64 postDmaReleaseMasterCycle_;
};

} // namespace snes

#endif
