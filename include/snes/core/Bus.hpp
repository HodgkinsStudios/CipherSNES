#ifndef SNES_CORE_BUS_HPP
#define SNES_CORE_BUS_HPP

#include "snes/Types.hpp"
#include "snes/core/CPU65816Bus.hpp"
#include <vector>

namespace snes {

class Memory;
class Cartridge;
class PPU;
class APU;
class DMA;
class Controller;
class Scheduler;

class Bus : public CPU65816Bus {
    friend class SaveStateManager;
public:
    struct ReadOverride {
        ReadOverride() : address(0u), value(0u), compare(0u), hasCompare(false) {}
        ReadOverride(u32 overrideAddress, u8 overrideValue, bool useCompare = false, u8 compareValue = 0u)
            : address(overrideAddress & 0xFFFFFFu), value(overrideValue), compare(compareValue), hasCompare(useCompare) {}
        u32 address;
        u8 value;
        u8 compare;
        bool hasCompare;
    };

    Bus();
    void connect(Memory* memory, Cartridge* cartridge, PPU* ppu,
                 APU* apu, DMA* dma, Controller* controller1,
                 Controller* controller2, Controller* controller3 = 0,
                 Controller* controller4 = 0, Controller* controller5 = 0);
    void setMultitapPort2(bool enabled);
    bool multitapPort2() const;
    void connectScheduler(Scheduler* scheduler);
    void reset();
    u8 read(u32 address);
    void write(u32 address, u8 value);
    u16 read16(u32 address);
    void write16(u32 address, u16 value);
    void setReadOverrides(const std::vector<ReadOverride>& overrides);
    void clearReadOverrides();

    bool nmiEnabled() const;
    bool consumeNmiEdge();
    void notifyVBlankStart();
    void notifyFrameStart();
    bool consumeNmiFlag();
    bool irqPending() const;
    bool cartridgeIrqPending() const;
    bool hvIrqEnabled() const;
    bool cpuIrqTwoClocksAgo() const;
    void clearIrq();
    void updateIrqPosition(int scanline, int dot, int totalScanlines);
    void stepIrqMasterClocks(unsigned clocks, int scanline, int lineClock, int lineLength);
    u8 openBus() const;
    // DMA reading an electrically undriven A/B bus samples zero on the
    // Ricoh 5A22; the result also replaces its CPU memory data register.
    u8 sampleInvalidDmaBus();
    u8 nmitimen() const;
    u16 hTimer() const;
    u16 vTimer() const;
    void beginCpuInstruction();
    unsigned consumeAccessMasterCycles();
    unsigned consumeCpuBusAccesses();
    void stepMasterCycles(unsigned cycles);
    void advanceMathCpuCycles(unsigned cycles);
    void traceTimingEvent(const char* kind, u32 address = 0, u32 value = 0) const;
    void beginDmaBusOwnership();
    void endDmaBusOwnership();
    void clockInternalMasterCycles(unsigned cycles);
    void beginCartridgeDma(unsigned channel, u32 sourceAddress, unsigned length);
    void endCartridgeDma(unsigned channel);

private:
    bool isSystemBank(u8 bank) const;
    u8 readCpuRegister(u16 address);
    void writeCpuRegister(u16 address, u8 value);
    unsigned accessMasterCycles(u32 address) const;
    void recordAccess(u32 address);
    void advanceMathCycle();
    void schedulePort2LightGun();
    void evaluateIrqComparator(bool registerWrite = false);
    void tickIrqCounter(int scanline, int lineClock);

    Memory* memory_;
    Cartridge* cartridge_;
    PPU* ppu_;
    APU* apu_;
    DMA* dma_;
    Controller* controller1_;
    Controller* controller2_;
    Controller* controller3_;
    Controller* controller4_;
    Controller* controller5_;
    Scheduler* scheduler_;

    u8 openBus_;
    u8 nmitimen_;
    bool nmiFlag_;
    bool nmiEdgePending_;
    bool irqFlag_;
    u8 pio_;
    u32 wramAddress_;
    u8 multiplicand_;
    u16 dividend_;
    u16 quotient_;
    u16 productOrRemainder_;
    u8 memsel_;
    u16 hTimer_;
    u16 vTimer_;
    int lastIrqScanline_;
    int lastIrqDot_;
    unsigned irqHCounter_;
    unsigned irqVCounter_;
    unsigned irqCountdown_;
    bool irqCompareLevel_;
    bool irqCpuLine_;
    bool irqLineOneClockAgo_;
    bool irqLineTwoClocksAgo_;
    unsigned accessMasterCycles_;
    unsigned cpuBusAccesses_;
    unsigned autoJoypadBusyMasterCycles_;
    unsigned mathBusyMasterCycles_;
    u8 mathOperation_;
    u32 mathShift_;
    unsigned mathPartialMasterClocks_;
    u16 autoJoypadResult_[4];
    bool dmaBusOwned_;
    bool multitapPort2_;
    std::vector<ReadOverride> readOverrides_;
};

} // namespace snes

#endif
