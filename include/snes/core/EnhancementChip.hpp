#ifndef SNES_CORE_ENHANCEMENT_CHIP_HPP
#define SNES_CORE_ENHANCEMENT_CHIP_HPP

#include <deque>
#include <string>
#include <vector>
#include "snes/Types.hpp"
#include "snes/core/Component.hpp"
#include "snes/core/CPU65816.hpp"
#include "snes/core/CPU65816Bus.hpp"

namespace snes {

// Standalone cartridge-hardware implementation.  The class deliberately owns
// all chip state so cartridge reads remain cheap and no external emulator core
// or runtime plug-in is required.
class EnhancementChip : public Component, private CPU65816Bus {
    friend class SaveStateManager;
public:
    enum Kind {
        None,
        DSP,
        SuperFX,
        SA1,
        CX4,
        SDD1,
        SPC7110,
        SPC7110RTC
    };

    EnhancementChip();
    // CPU65816 stores a bus pointer.  Cartridge is move-assigned during
    // transactional ROM loading, so reconnect that pointer to this instance
    // after a move instead of retaining the temporary cartridge's address.
    void rebindRuntime();
    void configure(Kind kind, u8 mapMode, u8 cartridgeType,
                   const std::string& title, std::size_t romSize,
                   std::size_t cartridgeRamSize);
    void reset();
    void step(unsigned masterClocks, const std::vector<u8>& rom,
              std::vector<u8>& cartridgeRam, bool& ramDirty);
    void beginDma(unsigned channel, u32 sourceAddress, unsigned length);
    void endDma(unsigned channel);

    bool read(u32 address, const std::vector<u8>& rom,
              std::vector<u8>& cartridgeRam, u8& value);
    bool write(u32 address, u8 value, const std::vector<u8>& rom,
               std::vector<u8>& cartridgeRam, bool& ramDirty);
    bool mapRomAddress(u32 address, std::size_t romSize,
                       std::size_t& mapped) const;
    bool exportRtcState(std::vector<u8>& data) const;
    bool importRtcState(const std::vector<u8>& data);

    Kind kind() const;
    bool irqPending() const;
    const char* implementationStatus() const;
    u64 operations() const;
    unsigned secondaryCpuTraceCount() const;
    const CPU65816::InstructionTrace& secondaryCpuTrace(unsigned index) const;
    unsigned superFxTraceCount() const;
    u32 superFxTraceAddress(unsigned index) const;
    u8 superFxTraceOpcode(unsigned index) const;
    u8 diagnosticRegister(unsigned index) const;

private:
    static s16 clamp16(s32 value);
    static s32 sign24(u32 value);
    static u16 read16(const std::vector<u8>& memory, std::size_t address);
    static u32 read24(const std::vector<u8>& memory, std::size_t address);
    static void write16(std::vector<u8>& memory, std::size_t address, u16 value);
    static void write24(std::vector<u8>& memory, std::size_t address, u32 value);
    static void write48(std::vector<u8>& memory, std::size_t address, s64 value);

    bool isDspPort(u32 address) const;
    unsigned dspExpectedBytes(u8 command) const;
    bool executeDspCommand();
    void pushDspWord(s16 value);
    void pushDspUnsigned(u16 value);

    bool readCx4(u32 address, u8& value) const;
    bool writeCx4(u32 address, u8 value);
    void executeCx4Command(u8 command);

    bool readSa1(u32 address, const std::vector<u8>& rom,
                 std::vector<u8>& cartridgeRam, u8& value);
    bool writeSa1(u32 address, u8 value, const std::vector<u8>& rom,
                  std::vector<u8>& cartridgeRam, bool& ramDirty);
    void executeSa1Math();
    void executeSa1Dma(const std::vector<u8>& rom,
                       std::vector<u8>& cartridgeRam, bool& ramDirty);
    void executeSa1CharacterConversion2(bool& ramDirty);
    void updateSa1IrqLine();
    void stepSa1(unsigned masterClocks, const std::vector<u8>& rom,
                 std::vector<u8>& cartridgeRam, bool& ramDirty);
    u8 read(u32 address);
    void write(u32 address, u8 value);
    std::size_t sa1MapRom(u32 address, std::size_t romSize) const;
    std::size_t sa1MapBwram(u32 address, bool sa1Side,
                           std::size_t ramSize) const;

    bool readSuperFx(u32 address, const std::vector<u8>& rom,
                     std::vector<u8>& cartridgeRam, u8& value);
    bool writeSuperFx(u32 address, u8 value, std::vector<u8>& cartridgeRam,
                      bool& ramDirty);
    void stepSuperFx(unsigned clocks, const std::vector<u8>& rom,
                     std::vector<u8>& cartridgeRam, bool& ramDirty);
    u8 superFxReadProgram(const std::vector<u8>& rom,
                          const std::vector<u8>& cartridgeRam, u32 address) const;

    bool readSdd1(u32 address, const std::vector<u8>& rom, u8& value);
    bool writeSdd1(u32 address, u8 value);
    std::size_t sdd1MapAddress(u32 address, std::size_t romSize) const;
    void initializeSdd1Stream(const std::vector<u8>& rom);
    unsigned sdd1ReadBit(const std::vector<u8>& rom);
    unsigned sdd1GolombBit(unsigned code, bool& endOfRun,
                           const std::vector<u8>& rom);
    unsigned sdd1ContextBit(unsigned plane, const std::vector<u8>& rom);
    u8 sdd1DecompressByte(const std::vector<u8>& rom);

    bool readSpc7110(u32 address, const std::vector<u8>& rom,
                     const std::vector<u8>& cartridgeRam, u8& value);
    bool writeSpc7110(u32 address, u8 value, const std::vector<u8>& rom,
                      std::vector<u8>& cartridgeRam, bool& ramDirty);
    u8 spc7110DataByte(const std::vector<u8>& rom, u32 address) const;
    void initializeSpc7110Decompressor(const std::vector<u8>& rom);
    void spc7110DecompressionCore(unsigned context, unsigned& decoded,
                                  const std::vector<u8>& rom);
    void spc7110MovePixelToFront(unsigned a, unsigned b, unsigned c,
                                unsigned shift, unsigned decoded);
    u8 spc7110DecompressByte(const std::vector<u8>& rom);
    u8 spc7110ReadDataPort(const std::vector<u8>& rom, bool offsetPort);
    void executeSpc7110Multiply();
    void executeSpc7110Divide();
    void updateSpc7110Rtc();
    u8 readSpc7110Rtc();
    void writeSpc7110Rtc(u8 value);

    Kind kind_;
    u8 mapMode_;
    u8 cartridgeType_;
    std::string title_;
    std::size_t romSize_;
    u64 operations_;

    // DSP-1/2/3/4 serial command processor.
    unsigned dspVariant_;
    u8 dspCommand_;
    unsigned dspExpected_;
    unsigned dspStage_;
    u8 dspTransparent_;
    unsigned dspLengthA_;
    unsigned dspLengthB_;
    std::vector<u8> dspInput_;
    std::deque<u8> dspOutput_;

    // CX4's 8 KiB internal RAM/register window.
    std::vector<u8> cx4Ram_;

    // SA-1 shared memories and MMIO state.  The S-CPU-visible DMA, arithmetic,
    // variable-bit and interrupt/control blocks are implemented here.
    std::vector<u8> sa1Iram_;
    std::vector<u8> sa1Bwram_;
    std::vector<u8> sa1Regs_;
    s64 sa1ArithmeticResult_;
    bool sa1Overflow_;
    CPU65816 sa1Cpu_;
    const std::vector<u8>* sa1ActiveRom_;
    std::vector<u8>* sa1ActiveBwram_;
    bool* sa1ActiveRamDirty_;
    s64 sa1ClockRemainder_;
    bool sa1Running_;
    u32 sa1TimerClocks_;
    u32 sa1VariableAddress_;
    unsigned sa1VariableBitOffset_;
    u16 sa1LatchedH_;
    u16 sa1LatchedV_;
    unsigned sa1Cc2Line_;
    unsigned sa1Cc2Tile_;

    // Super FX GSU register file, cache and cartridge RAM view.
    u16 superFxR_[16];
    u16 superFxStatus_;
    u8 superFxPbr_;
    u8 superFxRombr_;
    u8 superFxRambr_;
    u8 superFxColor_;
    u8 superFxAlt_;
    u8 superFxRegisterLatch_;
    u8 superFxSourceRegister_;
    u8 superFxDestinationRegister_;
    u8 superFxRomBuffer_;
    u8 superFxScreenBase_;
    u8 superFxScreenMode_;
    u8 superFxConfig_;
    u8 superFxClockSelect_;
    u8 superFxPlotOption_;
    u16 superFxCacheBase_;
    u16 superFxLastRamAddress_;
    bool superFxBranchPending_;
    u16 superFxBranchTarget_;
    bool superFxBankPending_;
    u8 superFxBankTarget_;
    unsigned superFxClockRemainder_;
    std::vector<u8> superFxCache_;
    std::vector<u8> superFxCacheValid_;
    std::vector<u8> superFxRam_;
    u32 superFxTraceAddress_[512];
    u8 superFxTraceOpcode_[512];
    unsigned superFxTracePosition_;
    unsigned superFxTraceCount_;

    // S-DD1 mapping/control registers.
    u8 sdd1Regs_[8];
    bool sdd1DmaActive_;
    bool sdd1StreamInitialized_;
    unsigned sdd1DmaChannel_;
    u32 sdd1DmaSource_;
    unsigned sdd1DmaRemaining_;
    u64 sdd1InputBitPosition_;
    u32 sdd1OutputBytePosition_;
    u8 sdd1BitplaneMode_;
    u8 sdd1ContextMode_;
    u8 sdd1NumPlanes_;
    u8 sdd1Plane_;
    u8 sdd1YLocation_;
    u16 sdd1History_[8];
    u8 sdd1ContextState_[32];
    u8 sdd1ContextMps_[32];
    unsigned sdd1RunCount_[8];
    bool sdd1RunHasLps_[8];
    bool sdd1RunActive_[8];

    // SPC7110 data/decompression, mapping, arithmetic and RTC registers.
    std::vector<u8> spc7110Regs_;
    u32 spc7110DataPointer_;
    u32 spc7110DecompPointer_;
    u16 spc7110DecompLength_;
    u64 spc7110MathResult_;
    u16 spc7110Remainder_;
    bool spc7110DecompInitialized_;
    u8 spc7110DecompMode_;
    u32 spc7110DecompOutput_;
    u32 spc7110DecompBufferIndex_;
    u8 spc7110DecompLastPixel_;
    u16 spc7110DecompTop_;
    u16 spc7110DecompValue_;
    unsigned spc7110DecompInputBits_;
    u8 spc7110PixelOrder_[16];
    u8 spc7110RealOrder_[16];
    u8 spc7110ContextIndex_[32];
    u8 spc7110ContextInvert_[32];
    u8 spc7110BitplaneBuffer_[16];
    bool spc7110DataPortActive_;
    u8 spc7110OffsetWriteMask_;
    u8 spc7110RtcRegisters_[16];
    u8 spc7110RtcState_;
    u8 spc7110RtcIndex_;
    s64 spc7110RtcLastUpdate_;
    u64 spc7110RtcClockRemainder_;
};

} // namespace snes

#endif
