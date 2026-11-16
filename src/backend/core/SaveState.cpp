#include "snes/core/SaveState.hpp"
#include "snes/core/System.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <streambuf>
#include <vector>

namespace snes {
namespace {

static const char StateMagic[8] = { 'S','N','E','S','S','T','A','T' };
static const u32 StateVersion = 24;
static const char ChecksumMagic[4] = { 'C','R','C','!' };

struct Crc32Table {
    Crc32Table() {
        for (unsigned index = 0u; index < 256u; ++index) {
            u32 value = index;
            for (unsigned bit = 0u; bit < 8u; ++bit)
                value = (value >> 1) ^ ((value & 1u) ? 0xEDB88320u : 0u);
            values[index] = value;
        }
    }
    u32 values[256];
};

u32 crc32Update(u32 crc, const char* data, std::size_t size) {
    static const Crc32Table table;
    const u8* bytes = reinterpret_cast<const u8*>(data);
    for (std::size_t i = 0; i < size; ++i)
        crc = (crc >> 8) ^ table.values[(crc ^ bytes[i]) & 0xFFu];
    return crc;
}

class VectorOutputBuffer : public std::streambuf {
public:
    explicit VectorOutputBuffer(std::vector<u8>& output) : output_(output) {}
protected:
    virtual std::streamsize xsputn(const char* source, std::streamsize count) {
        if (count <= 0) return 0;
        const u8* begin = reinterpret_cast<const u8*>(source);
        output_.insert(output_.end(), begin, begin + static_cast<std::size_t>(count));
        return count;
    }
    virtual int_type overflow(int_type value) {
        if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
        output_.push_back(static_cast<u8>(traits_type::to_char_type(value)));
        return value;
    }
private:
    std::vector<u8>& output_;
};

class VectorInputBuffer : public std::streambuf {
public:
    VectorInputBuffer(const u8* data, std::size_t size) {
        char* begin = const_cast<char*>(reinterpret_cast<const char*>(data));
        setg(begin, begin, begin + size);
    }
protected:
    virtual pos_type seekoff(off_type offset, std::ios_base::seekdir direction,
                             std::ios_base::openmode mode) {
        if ((mode & std::ios_base::in) == 0) return pos_type(off_type(-1));
        char* next = 0;
        if (direction == std::ios_base::beg) next = eback() + offset;
        else if (direction == std::ios_base::cur) next = gptr() + offset;
        else if (direction == std::ios_base::end) next = egptr() + offset;
        if (!next || next < eback() || next > egptr()) return pos_type(off_type(-1));
        setg(eback(), next, egptr());
        return pos_type(next - eback());
    }
    virtual pos_type seekpos(pos_type position, std::ios_base::openmode mode) {
        return seekoff(static_cast<off_type>(position), std::ios_base::beg, mode);
    }
};

bool appendChecksumTrailer(const std::string& path, std::string& error) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) { error = "Could not reopen the save-state file for verification."; return false; }
    u32 crc = 0xFFFFFFFFu;
    char buffer[32768];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize count = input.gcount();
        if (count > 0) crc = crc32Update(crc, buffer, static_cast<std::size_t>(count));
    }
    if (!input.eof()) { error = "Could not verify the complete save-state file."; return false; }
    crc ^= 0xFFFFFFFFu;
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::app);
    if (!output) { error = "Could not append the save-state integrity trailer."; return false; }
    output.write(ChecksumMagic, sizeof(ChecksumMagic));
    output.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
    if (!output) { error = "Could not write the save-state integrity trailer."; return false; }
    return true;
}

bool validateChecksumTrailer(const std::string& path, std::streamoff& payloadSize,
                            std::string& error) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) { error = "Could not open the save-state file."; return false; }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < static_cast<std::streamoff>(sizeof(ChecksumMagic) + sizeof(u32))) {
        error = "The save-state file is truncated.";
        return false;
    }
    if (size > static_cast<std::streamoff>(64u * 1024u * 1024u)) {
        error = "The save-state file exceeds the 64 MiB safety limit.";
        return false;
    }
    payloadSize = size - static_cast<std::streamoff>(sizeof(ChecksumMagic) + sizeof(u32));
    input.seekg(payloadSize, std::ios::beg);
    char magic[sizeof(ChecksumMagic)];
    u32 expected = 0;
    input.read(magic, sizeof(magic));
    input.read(reinterpret_cast<char*>(&expected), sizeof(expected));
    if (!input || std::memcmp(magic, ChecksumMagic, sizeof(magic)) != 0) {
        error = "The save-state integrity trailer is missing or invalid.";
        return false;
    }
    input.clear();
    input.seekg(0, std::ios::beg);
    u32 actual = 0xFFFFFFFFu;
    char buffer[32768];
    std::streamoff remaining = payloadSize;
    while (remaining > 0) {
        const std::streamsize request = static_cast<std::streamsize>(
            std::min<std::streamoff>(remaining, static_cast<std::streamoff>(sizeof(buffer))));
        input.read(buffer, request);
        if (input.gcount() != request) {
            error = "The save-state file is truncated.";
            return false;
        }
        actual = crc32Update(actual, buffer, static_cast<std::size_t>(request));
        remaining -= request;
    }
    actual ^= 0xFFFFFFFFu;
    if (actual != expected) {
        error = "The save-state checksum does not match; the file is corrupt or incomplete.";
        return false;
    }
    return true;
}

template<typename T>
bool writeValue(std::ostream& output, const T& value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(output);
}

template<typename T>
bool readValue(std::istream& input, T& value) {
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(input);
}

template<typename T>
bool writeVector(std::ostream& output, const std::vector<T>& values) {
    const u32 count = static_cast<u32>(values.size());
    if (!writeValue(output, count)) return false;
    if (count) output.write(reinterpret_cast<const char*>(&values[0]), static_cast<std::streamsize>(count * sizeof(T)));
    return static_cast<bool>(output);
}

template<typename T>
bool readVector(std::istream& input, std::vector<T>& values, u32 maximum) {
    u32 count = 0;
    if (!readValue(input, count) || count > maximum) return false;
    values.resize(count);
    if (count) input.read(reinterpret_cast<char*>(&values[0]), static_cast<std::streamsize>(count * sizeof(T)));
    return static_cast<bool>(input);
}

template<typename T>
bool writeDeque(std::ostream& output, const std::deque<T>& values) {
    const u32 count = static_cast<u32>(values.size());
    if (!writeValue(output, count)) return false;
    for (typename std::deque<T>::const_iterator it = values.begin(); it != values.end(); ++it)
        if (!writeValue(output, *it)) return false;
    return true;
}

template<typename T>
bool readDeque(std::istream& input, std::deque<T>& values, u32 maximum) {
    u32 count = 0;
    if (!readValue(input, count) || count > maximum) return false;
    values.clear();
    for (u32 i = 0; i < count; ++i) { T value; if (!readValue(input, value)) return false; values.push_back(value); }
    return true;
}

u32 romIdentity(const std::vector<u8>& rom) {
    u32 hash = 2166136261u;
    for (std::size_t i = 0; i < rom.size(); ++i) {
        hash ^= rom[i];
        hash *= 16777619u;
    }
    return hash;
}



} // namespace

SaveStateManager::SaveStateManager() {}

bool SaveStateManager::save(const System& system, const std::string& path, std::string& error) const {
    error.clear();
    if (!system.cartridge_.loaded_) {
        error = "A cartridge must be loaded before saving a state.";
        return false;
    }
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) { error = "Could not create the save-state file."; return false; }
    if (!savePayload(system, out, error)) return false;
    out.close();
    if (!out) { error = "The save-state file could not be finalized."; return false; }
    return appendChecksumTrailer(path, error);
}

bool SaveStateManager::savePayload(const System& system, std::ostream& out, std::string& error) const {
    out.write(StateMagic, sizeof(StateMagic));
    const u32 romSize = static_cast<u32>(system.cartridge_.rom_.size());
    const u32 identity = romIdentity(system.cartridge_.rom_);
    if (!writeValue(out, StateVersion) || !writeValue(out, romSize) || !writeValue(out, identity)) {
        error = "Could not write the save-state header."; return false;
    }

    const u32 state = static_cast<u32>(system.state_);
    if (!writeValue(out, state)) return false;

    const CPU65816& c = system.cpu_;
    if (!writeValue(out,c.a_)||!writeValue(out,c.x_)||!writeValue(out,c.y_)||!writeValue(out,c.s_)||
        !writeValue(out,c.d_)||!writeValue(out,c.pc_)||!writeValue(out,c.pbr_)||!writeValue(out,c.dbr_)||
        !writeValue(out,c.p_)||!writeValue(out,c.emulationMode_)||!writeValue(out,c.nmiPending_)||
        !writeValue(out,c.irqPending_)||!writeValue(out,c.waiting_)||!writeValue(out,c.stopped_)||
        !writeValue(out,c.cycles_)||!writeValue(out,c.instructions_)) return false;

    if (!writeVector(out, system.memory_.wram_) || !writeVector(out, system.cartridge_.sram_)) return false;
    if (!writeValue(out, system.cartridge_.ramDirty_) ||
        !writeValue(out, system.cartridge_.obc1Enabled_) ||
        !writeValue(out, system.cartridge_.obc1Base_) ||
        !writeValue(out, system.cartridge_.obc1Address_) ||
        !writeValue(out, system.cartridge_.obc1Shift_)) return false;

    const EnhancementChip& ec = system.cartridge_.enhancementChip_;
    if (!writeValue(out, ec.kind_) || !writeValue(out, ec.mapMode_) ||
        !writeValue(out, ec.cartridgeType_) || !writeValue(out, ec.romSize_) ||
        !writeValue(out, ec.operations_) || !writeValue(out, ec.dspVariant_) ||
        !writeValue(out, ec.dspCommand_) || !writeValue(out, ec.dspExpected_) ||
        !writeValue(out, ec.dspStage_) || !writeValue(out, ec.dspTransparent_) ||
        !writeValue(out, ec.dspLengthA_) || !writeValue(out, ec.dspLengthB_) ||
        !writeVector(out, ec.dspInput_) || !writeDeque(out, ec.dspOutput_) ||
        !writeVector(out, ec.cx4Ram_) || !writeVector(out, ec.sa1Iram_) ||
        !writeVector(out, ec.sa1Bwram_) || !writeVector(out, ec.sa1Regs_) ||
        !writeValue(out, ec.sa1ArithmeticResult_) || !writeValue(out, ec.sa1Overflow_)) return false;
    const CPU65816& sc = ec.sa1Cpu_;
    if (!writeValue(out,sc.a_)||!writeValue(out,sc.x_)||!writeValue(out,sc.y_)||!writeValue(out,sc.s_)||
        !writeValue(out,sc.d_)||!writeValue(out,sc.pc_)||!writeValue(out,sc.pbr_)||!writeValue(out,sc.dbr_)||
        !writeValue(out,sc.p_)||!writeValue(out,sc.emulationMode_)||!writeValue(out,sc.nmiPending_)||
        !writeValue(out,sc.irqPending_)||!writeValue(out,sc.waiting_)||!writeValue(out,sc.stopped_)||
        !writeValue(out,sc.cycles_)||!writeValue(out,sc.instructions_)||
        !writeValue(out,ec.sa1ClockRemainder_)||!writeValue(out,ec.sa1Running_)||
        !writeValue(out,ec.sa1TimerClocks_)||!writeValue(out,ec.sa1VariableAddress_)||
        !writeValue(out,ec.sa1VariableBitOffset_)||!writeValue(out,ec.sa1LatchedH_)||
        !writeValue(out,ec.sa1LatchedV_)||!writeValue(out,ec.sa1Cc2Line_)||
        !writeValue(out,ec.sa1Cc2Tile_)) return false;
    for (unsigned i = 0; i < 16u; ++i) if (!writeValue(out, ec.superFxR_[i])) return false;
    if (!writeValue(out, ec.superFxStatus_) || !writeValue(out, ec.superFxPbr_) ||
        !writeValue(out, ec.superFxRombr_) || !writeValue(out, ec.superFxRambr_) ||
        !writeValue(out, ec.superFxColor_) || !writeValue(out, ec.superFxAlt_) ||
        !writeValue(out, ec.superFxRegisterLatch_) ||
        !writeValue(out, ec.superFxSourceRegister_) || !writeValue(out, ec.superFxDestinationRegister_) ||
        !writeValue(out, ec.superFxRomBuffer_) || !writeValue(out, ec.superFxScreenBase_) ||
        !writeValue(out, ec.superFxScreenMode_) || !writeValue(out, ec.superFxConfig_) ||
        !writeValue(out, ec.superFxClockSelect_) || !writeValue(out, ec.superFxPlotOption_) ||
        !writeValue(out, ec.superFxCacheBase_) || !writeValue(out, ec.superFxLastRamAddress_) ||
        !writeValue(out, ec.superFxBranchPending_) || !writeValue(out, ec.superFxBranchTarget_) ||
        !writeValue(out, ec.superFxBankPending_) || !writeValue(out, ec.superFxBankTarget_) ||
        !writeValue(out, ec.superFxClockRemainder_) || !writeVector(out, ec.superFxCache_) ||
        !writeVector(out, ec.superFxCacheValid_) || !writeVector(out, ec.superFxRam_)) return false;
    for (unsigned i = 0; i < 8u; ++i) if (!writeValue(out, ec.sdd1Regs_[i])) return false;
    if (!writeValue(out,ec.sdd1DmaActive_)||!writeValue(out,ec.sdd1StreamInitialized_)||
        !writeValue(out,ec.sdd1DmaChannel_)||!writeValue(out,ec.sdd1DmaSource_)||
        !writeValue(out,ec.sdd1DmaRemaining_)||!writeValue(out,ec.sdd1InputBitPosition_)||
        !writeValue(out,ec.sdd1OutputBytePosition_)||!writeValue(out,ec.sdd1BitplaneMode_)||
        !writeValue(out,ec.sdd1ContextMode_)||!writeValue(out,ec.sdd1NumPlanes_)||
        !writeValue(out,ec.sdd1Plane_)||!writeValue(out,ec.sdd1YLocation_)) return false;
    for (unsigned i=0;i<8u;++i) if(!writeValue(out,ec.sdd1History_[i])||
        !writeValue(out,ec.sdd1RunCount_[i])||!writeValue(out,ec.sdd1RunHasLps_[i])||
        !writeValue(out,ec.sdd1RunActive_[i])) return false;
    for (unsigned i=0;i<32u;++i) if(!writeValue(out,ec.sdd1ContextState_[i])||
        !writeValue(out,ec.sdd1ContextMps_[i])) return false;
    if (!writeVector(out, ec.spc7110Regs_) || !writeValue(out, ec.spc7110DataPointer_) ||
        !writeValue(out, ec.spc7110DecompPointer_) || !writeValue(out, ec.spc7110DecompLength_) ||
        !writeValue(out, ec.spc7110MathResult_) || !writeValue(out, ec.spc7110Remainder_) ||
        !writeValue(out, ec.spc7110DecompInitialized_) || !writeValue(out, ec.spc7110DecompMode_) ||
        !writeValue(out, ec.spc7110DecompOutput_) || !writeValue(out, ec.spc7110DecompBufferIndex_) ||
        !writeValue(out, ec.spc7110DecompLastPixel_) || !writeValue(out, ec.spc7110DecompTop_) ||
        !writeValue(out, ec.spc7110DecompValue_) || !writeValue(out, ec.spc7110DecompInputBits_) ||
        !writeValue(out, ec.spc7110DataPortActive_) || !writeValue(out, ec.spc7110OffsetWriteMask_) ||
        !writeValue(out, ec.spc7110RtcState_) || !writeValue(out, ec.spc7110RtcIndex_) ||
        !writeValue(out, ec.spc7110RtcLastUpdate_) || !writeValue(out, ec.spc7110RtcClockRemainder_)) return false;
    for(unsigned i=0;i<16u;++i) if(!writeValue(out,ec.spc7110PixelOrder_[i])||
        !writeValue(out,ec.spc7110RealOrder_[i])||!writeValue(out,ec.spc7110BitplaneBuffer_[i])||
        !writeValue(out,ec.spc7110RtcRegisters_[i])) return false;
    for(unsigned i=0;i<32u;++i) if(!writeValue(out,ec.spc7110ContextIndex_[i])||
        !writeValue(out,ec.spc7110ContextInvert_[i])) return false;

    const PPU& p = system.ppu_;
    if (!writeVector(out,p.frameBuffer_)||!writeVector(out,p.layerBuffer_)||!writeVector(out,p.vram_)||
        !writeVector(out,p.cgram_)||!writeVector(out,p.oam_)||
        !writeVector(out,p.interlaceFrameScratch_)||!writeVector(out,p.interlaceLayerScratch_)) return false;
    for (unsigned i=0;i<4;++i) {
        if (!writeValue(out,p.backgrounds_[i].screenBaseAndSize)||!writeValue(out,p.backgrounds_[i].tileBase)||
            !writeValue(out,p.backgrounds_[i].hOffset)||!writeValue(out,p.backgrounds_[i].vOffset)) return false;
    }
#define WPPU(v) if(!writeValue(out,p.v)) return false
    WPPU(outputWidth_); WPPU(outputHeight_); WPPU(renderWidth_);
    WPPU(scanline_); WPPU(dot_); WPPU(masterClock_); WPPU(lineMasterClocks_);
    WPPU(totalScanlines_); WPPU(frameNumber_); WPPU(pal_); WPPU(field_); WPPU(vblank_); WPPU(frameReady_);
    WPPU(hblankStartEvent_); WPPU(hdmaStartEvent_); WPPU(vblankStartEvent_); WPPU(frameStartEvent_); WPPU(refreshStartEvent_);
    WPPU(rangeOver_); WPPU(timeOver_); WPPU(renderFrameEnabled_); WPPU(frameSurfacePrepared_); WPPU(frameHadHires_); WPPU(renderedPixelsThisLine_);
    WPPU(blockedVramAccesses_); WPPU(blockedCgramAccesses_); WPPU(blockedOamAccesses_);
    WPPU(displayControl_); WPPU(objectSelection_); WPPU(bgMode_); WPPU(mosaic_); WPPU(mainScreen_); WPPU(subScreen_);
    WPPU(setIni_); WPPU(vmain_); WPPU(vramAddress_); WPPU(vramReadBuffer_); WPPU(oamReloadAddress_); WPPU(oamAddress_);
    WPPU(oamWriteLatch_); WPPU(oamWriteHigh_); WPPU(oamPriorityRotation_); WPPU(oamFirstSprite_); WPPU(cgramAddress_);
    WPPU(cgramHigh_); WPPU(cgramLatch_); WPPU(scrollLatch_); WPPU(horizontalScrollLatch_);
    WPPU(hCounterLatch_); WPPU(vCounterLatch_); WPPU(hCounterHigh_); WPPU(vCounterHigh_);
    WPPU(mode7Control_); WPPU(mode7A_); WPPU(mode7B_); WPPU(mode7C_); WPPU(mode7D_);
    WPPU(mode7X_); WPPU(mode7Y_); WPPU(mode7HOffset_); WPPU(mode7VOffset_); WPPU(mode7Latch_); WPPU(mode7SecondWrite_);
    WPPU(ppu1OpenBus_); WPPU(ppu2OpenBus_); WPPU(countersLatched_); WPPU(counterLatchEnabled_);
    WPPU(counterLatchPending_); WPPU(pendingCounterH_); WPPU(pendingCounterV_); WPPU(pendingCounterFrame_);
    for(unsigned i=0;i<3;++i) WPPU(windowSelect_[i]);
    for(unsigned i=0;i<2;++i) { WPPU(windowLeft_[i]); WPPU(windowRight_[i]); WPPU(windowLogic_[i]); }
    WPPU(mainWindowMask_); WPPU(subWindowMask_); WPPU(colorMathControl_); WPPU(colorMathEnable_); WPPU(fixedColor_);
#undef WPPU

    const APU& a = system.apu_;
    if (!writeValue(out,a.cycles_)||!writeValue(out,a.executionBudget_)||!writeValue(out,a.dspClockRemainder_)||!writeValue(out,a.slowCycleRemainder_)) return false;
    out.write(reinterpret_cast<const char*>(a.ram_), sizeof(a.ram_));
    for(unsigned i=0;i<4;++i) if(!writeValue(out,a.cpuToApu_[i])||!writeValue(out,a.apuToCpu_[i])) return false;
    if(!writeValue(out,a.control_)||!writeValue(out,a.test_)||!writeValue(out,a.dspAddress_)||!writeValue(out,a.iplRomEnabled_)) return false;
    for(unsigned i=0;i<3;++i) {
        const APU::Timer& t=a.timers_[i];
        if(!writeValue(out,t.enabled)||!writeValue(out,t.target)||!writeValue(out,t.divider)||
           !writeValue(out,t.output)||!writeValue(out,t.baseCounter)) return false;
    }
    const SPC700& spc=a.spc700_;
    if(!writeValue(out,spc.pc_)||!writeValue(out,spc.a_)||!writeValue(out,spc.x_)||!writeValue(out,spc.y_)||
       !writeValue(out,spc.sp_)||!writeValue(out,spc.psw_)||!writeValue(out,spc.cycles_)||
       !writeValue(out,spc.instructions_)||!writeValue(out,spc.stopped_)) return false;
    const DSP& dsp=a.dsp_;
    out.write(reinterpret_cast<const char*>(dsp.registers_), sizeof(dsp.registers_));
    if(!writeValue(out,dsp.noiseLfsr_)||!writeValue(out,dsp.noiseCounter_)||!writeValue(out,dsp.rateCounter_)||
       !writeValue(out,dsp.echoHistoryIndex_)||!writeValue(out,dsp.echoOffset_)||
       !writeValue(out,dsp.echoBaseLatched_)||!writeValue(out,dsp.echoLengthLatched_)) return false;
    out.write(reinterpret_cast<const char*>(dsp.echoHistoryLeft_), sizeof(dsp.echoHistoryLeft_));
    out.write(reinterpret_cast<const char*>(dsp.echoHistoryRight_), sizeof(dsp.echoHistoryRight_));
    for(unsigned i=0;i<DSP::VoiceCount;++i) {
        const DSP::Voice& v=dsp.voices_[i];
#define WVOICE(field) if(!writeValue(out,v.field)) return false
        WVOICE(pitch); WVOICE(sourceNumber); WVOICE(leftVolume); WVOICE(rightVolume); WVOICE(keyedOn);
        WVOICE(phase); WVOICE(brrAddress); WVOICE(loopAddress); WVOICE(brrNibble); WVOICE(brrHeader);
        WVOICE(history1); WVOICE(history2); WVOICE(envelope); WVOICE(envelopeState);
        WVOICE(envelopeCounter); WVOICE(ended);
#undef WVOICE
        out.write(reinterpret_cast<const char*>(v.decoded), sizeof(v.decoded));
    }
    if(!writeVector(out,dsp.pendingSamples_)) return false;
    if(!writeValue(out,dsp.lastOutputLeft_)||!writeValue(out,dsp.lastOutputRight_)||
       !writeValue(out,dsp.recentPeak_)||!writeValue(out,dsp.samplesGenerated_)||
       !writeValue(out,dsp.nonSilentSamples_)) return false;

    const DMA& dma=system.dma_;
    for(unsigned i=0;i<8;++i) {
        const DMA::Channel& ch=dma.channels_[i];
#define WCH(v) if(!writeValue(out,ch.v)) return false
        WCH(control); WCH(bAddress); WCH(aAddress); WCH(aBank); WCH(size); WCH(indirectBank);
        WCH(tableAddress); WCH(lineCounter); WCH(unused); WCH(indirectAddress);
        WCH(hdmaLinesRemaining); WCH(hdmaActive); WCH(hdmaRepeat); WCH(hdmaDoTransfer);
#undef WCH
    }
    if(!writeValue(out,dma.active_)||!writeValue(out,dma.activeChannels_)||!writeValue(out,dma.hdmaEnable_)||
       !writeValue(out,dma.bytesTransferred_)) return false;
    const Controller* controllers[] = { &system.controller1_, &system.controller2_, &system.controller3_,
                                        &system.controller4_, &system.controller5_ };
    for (unsigned i = 0; i < 5u; ++i) {
        const Controller& c = *controllers[i];
        const u8 device = static_cast<u8>(c.device_);
        if(!writeValue(out,c.state_)||!writeValue(out,c.shift_)||!writeValue(out,c.serialPosition_)||
           !writeValue(out,c.latched_)||!writeValue(out,device)||!writeValue(out,c.mouseSensitivity_)||
           !writeValue(out,c.mouseLeft_)||!writeValue(out,c.mouseRight_)||
           !writeValue(out,c.mouseXNegative_)||!writeValue(out,c.mouseYNegative_)||
           !writeValue(out,c.mouseDeltaX_)||!writeValue(out,c.mouseDeltaY_)) return false;
        for(unsigned button=0;button<5u;++button) if(!writeValue(out,c.lightGunButtons_[button])) return false;
        if(!writeValue(out,c.scopeTurboSwitch_)||!writeValue(out,c.scopeTurboReported_)||
           !writeValue(out,c.scopeFirePulse_)||!writeValue(out,c.scopePausePulse_)||
           !writeValue(out,c.scopeFireSerial_)||!writeValue(out,c.scopePauseSerial_)) return false;
        for(unsigned gun=0;gun<2u;++gun)
            if(!writeValue(out,c.lightGunX_[gun])||!writeValue(out,c.lightGunY_[gun])||
               !writeValue(out,c.lightGunOffscreen_[gun])) return false;
        if(!writeValue(out,c.justifierLatchedGun_)||!writeValue(out,c.justifierNextGun_)) return false;
    }

    const Bus& b=system.bus_;
#define WBUS(v) if(!writeValue(out,b.v)) return false
    WBUS(openBus_); WBUS(nmitimen_); WBUS(nmiFlag_); WBUS(nmiEdgePending_); WBUS(irqFlag_); WBUS(pio_); WBUS(wramAddress_);
    WBUS(multiplicand_); WBUS(dividend_); WBUS(quotient_); WBUS(productOrRemainder_); WBUS(memsel_);
    WBUS(hTimer_); WBUS(vTimer_); WBUS(lastIrqScanline_); WBUS(lastIrqDot_); WBUS(accessMasterCycles_); WBUS(cpuBusAccesses_);
    WBUS(autoJoypadBusyMasterCycles_); WBUS(mathBusyMasterCycles_); WBUS(mathOperation_);
    WBUS(mathShift_); WBUS(mathPartialMasterClocks_);
    for (unsigned i = 0; i < 4; ++i) WBUS(autoJoypadResult_[i]);
    WBUS(multitapPort2_);
    WBUS(irqHCounter_); WBUS(irqVCounter_); WBUS(irqCountdown_); WBUS(irqCompareLevel_); WBUS(irqCpuLine_);
#undef WBUS
    const Scheduler& sch=system.scheduler_;
    if(!writeValue(out,sch.masterCycles_)||!writeValue(out,sch.framesRun_)||
       !writeValue(out,sch.ppuRemainder_)||!writeValue(out,sch.apuRemainder_)) return false;
    // v21: the CPU's cycle-early IRQ sample and the last two master-clock
    // IRQ line values must round-trip for deterministic resume.
    if (!writeValue(out,c.sampledInstructionIrq_)||
        !writeValue(out,b.irqLineOneClockAgo_)||
        !writeValue(out,b.irqLineTwoClocksAgo_)||
        !writeValue(out,c.sampledInstructionNmi_)) return false;
    // v23: the post-$420B interrupt-inhibit window must survive a save at
    // an instruction boundary immediately after DMA, not only full frames.
    if (!writeValue(out,sch.postDmaInterruptInhibit_) ||
        !writeValue(out,sch.postDmaReleaseMasterCycle_)) return false;
    // v24: previous/current OBJ fetch availability survives a save made
    // halfway through raster-time HBlank display control transitions.
    if (!writeValue(out,p.objFetchBlockedCurrent_) ||
        !writeValue(out,p.objFetchBlockedPending_)) return false;

    if (!out) { error = "The save-state stream could not be written completely."; return false; }
    return true;
}

bool SaveStateManager::saveToMemory(const System& system, std::vector<u8>& output, std::string& error) const {
    if (!saveTransientToMemory(system, output, error)) return false;
    const u32 crc = crc32Update(0xFFFFFFFFu, reinterpret_cast<const char*>(&output[0]), output.size()) ^ 0xFFFFFFFFu;
    output.insert(output.end(), ChecksumMagic, ChecksumMagic + sizeof(ChecksumMagic));
    const u8* crcBytes = reinterpret_cast<const u8*>(&crc);
    output.insert(output.end(), crcBytes, crcBytes + sizeof(crc));
    return true;
}

bool SaveStateManager::saveTransientToMemory(const System& system, std::vector<u8>& output,
                                             std::string& error) const {
    error.clear();
    output.clear();
    if (!system.cartridge_.loaded_) { error = "A cartridge must be loaded before saving a state."; return false; }
    if (output.capacity() < 2u * 1024u * 1024u) output.reserve(2u * 1024u * 1024u);
    VectorOutputBuffer buffer(output);
    std::ostream stream(&buffer);
    if (!savePayload(system, stream, error)) { output.clear(); return false; }
    if (output.size() > 64u * 1024u * 1024u) {
        output.clear();
        error = "The save-state payload exceeds the 64 MiB safety limit.";
        return false;
    }
    return true;
}

bool SaveStateManager::load(System& system, const std::string& path, std::string& error) const {
    error.clear();
    if (!system.cartridge_.loaded_) { error = "Load the matching cartridge before loading a state."; return false; }
    std::streamoff payloadSize = 0;
    if (!validateChecksumTrailer(path, payloadSize, error)) return false;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) { error = "Could not open the save-state file."; return false; }
    return loadPayload(system, in, payloadSize, error);
}

bool SaveStateManager::loadFromMemory(System& system, const std::vector<u8>& input, std::string& error) const {
    error.clear();
    if (!system.cartridge_.loaded_) { error = "Load the matching cartridge before loading a state."; return false; }
    const std::size_t trailerSize = sizeof(ChecksumMagic) + sizeof(u32);
    if (input.size() < trailerSize) { error = "The save-state memory image is truncated."; return false; }
    if (input.size() > 64u * 1024u * 1024u) { error = "The save-state memory image exceeds the 64 MiB safety limit."; return false; }
    const std::size_t payloadBytes = input.size() - trailerSize;
    if (std::memcmp(&input[payloadBytes], ChecksumMagic, sizeof(ChecksumMagic)) != 0) {
        error = "The save-state memory checksum trailer is missing or invalid."; return false;
    }
    u32 expected = 0u;
    std::memcpy(&expected, &input[payloadBytes + sizeof(ChecksumMagic)], sizeof(expected));
    const u32 actual = crc32Update(0xFFFFFFFFu, reinterpret_cast<const char*>(&input[0]), payloadBytes) ^ 0xFFFFFFFFu;
    if (actual != expected) { error = "The save-state memory checksum does not match."; return false; }
    VectorInputBuffer buffer(&input[0], payloadBytes);
    std::istream stream(&buffer);
    return loadPayload(system, stream, static_cast<std::streamoff>(payloadBytes), error);
}

bool SaveStateManager::loadTransientFromMemory(System& system, const std::vector<u8>& input,
                                               std::string& error) const {
    error.clear();
    if (!system.cartridge_.loaded_) { error = "Load the matching cartridge before loading a state."; return false; }
    if (input.empty()) { error = "The transient save-state image is empty."; return false; }
    if (input.size() > 64u * 1024u * 1024u) { error = "The transient save-state image exceeds the 64 MiB safety limit."; return false; }
    VectorInputBuffer buffer(&input[0], input.size());
    std::istream stream(&buffer);
    return loadPayload(system, stream, static_cast<std::streamoff>(input.size()), error);
}

bool SaveStateManager::loadPayload(System& system, std::istream& in, std::streamoff payloadSize,
                                   std::string& error) const {
    char magic[8]; in.read(magic,sizeof(magic));
    u32 version=0,romSize=0,identity=0;
    if(!in||std::memcmp(magic,StateMagic,sizeof(magic))!=0||!readValue(in,version)||(version!=StateVersion && version!=18u && version!=19u && version!=20u && version!=21u && version!=22u && version!=23u)||
       !readValue(in,romSize)||!readValue(in,identity)) { error="The save-state header is invalid or unsupported."; return false; }
    if(romSize!=system.cartridge_.rom_.size()||identity!=romIdentity(system.cartridge_.rom_)) {
        error="This save state belongs to a different ROM."; return false;
    }
    u32 state=0; if(!readValue(in,state)||state>static_cast<u32>(System::Faulted)) {error="Invalid system state.";return false;}

    CPU65816& c=system.cpu_;
    if(!readValue(in,c.a_)||!readValue(in,c.x_)||!readValue(in,c.y_)||!readValue(in,c.s_)||
       !readValue(in,c.d_)||!readValue(in,c.pc_)||!readValue(in,c.pbr_)||!readValue(in,c.dbr_)||
       !readValue(in,c.p_)||!readValue(in,c.emulationMode_)||!readValue(in,c.nmiPending_)||
       !readValue(in,c.irqPending_)||!readValue(in,c.waiting_)||!readValue(in,c.stopped_)||
       !readValue(in,c.cycles_)||!readValue(in,c.instructions_)) {error="Truncated CPU state.";return false;}
    c.recentProgramCounterPosition_ = 0;
    c.recentProgramCounterCount_ = 0;
    for (unsigned i = 0; i < 256u; ++i) c.recentProgramCounters_[i] = 0;

    if(!readVector(in,system.memory_.wram_,128u*1024u)||
       !readVector(in,system.cartridge_.sram_,2u*1024u*1024u)||!readValue(in,system.cartridge_.ramDirty_)||
       !readValue(in,system.cartridge_.obc1Enabled_)||!readValue(in,system.cartridge_.obc1Base_)||
       !readValue(in,system.cartridge_.obc1Address_)||!readValue(in,system.cartridge_.obc1Shift_)) {
        error="Truncated memory state.";return false;
    }

    EnhancementChip& ec = system.cartridge_.enhancementChip_;
    if (!readValue(in, ec.kind_) || !readValue(in, ec.mapMode_) ||
        !readValue(in, ec.cartridgeType_) || !readValue(in, ec.romSize_) ||
        !readValue(in, ec.operations_) || !readValue(in, ec.dspVariant_) ||
        !readValue(in, ec.dspCommand_) || !readValue(in, ec.dspExpected_) ||
        !readValue(in, ec.dspStage_) || !readValue(in, ec.dspTransparent_) ||
        !readValue(in, ec.dspLengthA_) || !readValue(in, ec.dspLengthB_) ||
        !readVector(in, ec.dspInput_, 4096u) || !readDeque(in, ec.dspOutput_, 4096u) ||
        !readVector(in, ec.cx4Ram_, 0x2000u) || !readVector(in, ec.sa1Iram_, 0x800u) ||
        !readVector(in, ec.sa1Bwram_, 0x40000u) || !readVector(in, ec.sa1Regs_, 0x200u) ||
        !readValue(in, ec.sa1ArithmeticResult_) || !readValue(in, ec.sa1Overflow_)) {
        error="Truncated enhancement-chip state."; return false;
    }
    CPU65816& sc = ec.sa1Cpu_;
    if (!readValue(in,sc.a_)||!readValue(in,sc.x_)||!readValue(in,sc.y_)||!readValue(in,sc.s_)||
        !readValue(in,sc.d_)||!readValue(in,sc.pc_)||!readValue(in,sc.pbr_)||!readValue(in,sc.dbr_)||
        !readValue(in,sc.p_)||!readValue(in,sc.emulationMode_)||!readValue(in,sc.nmiPending_)||
        !readValue(in,sc.irqPending_)||!readValue(in,sc.waiting_)||!readValue(in,sc.stopped_)||
        !readValue(in,sc.cycles_)||!readValue(in,sc.instructions_)||
        !readValue(in,ec.sa1ClockRemainder_)||!readValue(in,ec.sa1Running_)||
        !readValue(in,ec.sa1TimerClocks_)||!readValue(in,ec.sa1VariableAddress_)||
        !readValue(in,ec.sa1VariableBitOffset_)||!readValue(in,ec.sa1LatchedH_)||
        !readValue(in,ec.sa1LatchedV_)||!readValue(in,ec.sa1Cc2Line_)||
        !readValue(in,ec.sa1Cc2Tile_)) {error="Truncated SA-1 CPU state.";return false;}
    sc.recentProgramCounterPosition_=sc.recentProgramCounterCount_=0;
    sc.instructionTracePosition_=sc.instructionTraceCount_=0;
    ec.sa1ActiveRom_=0;ec.sa1ActiveBwram_=0;ec.sa1ActiveRamDirty_=0;
    for (unsigned i = 0; i < 16u; ++i) if (!readValue(in, ec.superFxR_[i])) { error="Truncated Super FX state."; return false; }
    if (!readValue(in, ec.superFxStatus_) || !readValue(in, ec.superFxPbr_) ||
        !readValue(in, ec.superFxRombr_) || !readValue(in, ec.superFxRambr_) ||
        !readValue(in, ec.superFxColor_) || !readValue(in, ec.superFxAlt_) ||
        !readValue(in, ec.superFxRegisterLatch_) ||
        !readValue(in, ec.superFxSourceRegister_) || !readValue(in, ec.superFxDestinationRegister_) ||
        !readValue(in, ec.superFxRomBuffer_) || !readValue(in, ec.superFxScreenBase_) ||
        !readValue(in, ec.superFxScreenMode_) || !readValue(in, ec.superFxConfig_) ||
        !readValue(in, ec.superFxClockSelect_) || !readValue(in, ec.superFxPlotOption_) ||
        !readValue(in, ec.superFxCacheBase_) || !readValue(in, ec.superFxLastRamAddress_) ||
        !readValue(in, ec.superFxBranchPending_) || !readValue(in, ec.superFxBranchTarget_) ||
        !readValue(in, ec.superFxBankPending_) || !readValue(in, ec.superFxBankTarget_) ||
        !readValue(in, ec.superFxClockRemainder_) || !readVector(in, ec.superFxCache_, 512u) ||
        !readVector(in, ec.superFxCacheValid_, 32u) ||
        !readVector(in, ec.superFxRam_, 0x20000u)) { error="Truncated Super FX memory."; return false; }
    for (unsigned i = 0; i < 8u; ++i) if (!readValue(in, ec.sdd1Regs_[i])) { error="Truncated S-DD1 state."; return false; }
    if (!readValue(in,ec.sdd1DmaActive_)||!readValue(in,ec.sdd1StreamInitialized_)||
        !readValue(in,ec.sdd1DmaChannel_)||!readValue(in,ec.sdd1DmaSource_)||
        !readValue(in,ec.sdd1DmaRemaining_)||!readValue(in,ec.sdd1InputBitPosition_)||
        !readValue(in,ec.sdd1OutputBytePosition_)||!readValue(in,ec.sdd1BitplaneMode_)||
        !readValue(in,ec.sdd1ContextMode_)||!readValue(in,ec.sdd1NumPlanes_)||
        !readValue(in,ec.sdd1Plane_)||!readValue(in,ec.sdd1YLocation_)) {error="Truncated S-DD1 stream state.";return false;}
    for(unsigned i=0;i<8u;++i) if(!readValue(in,ec.sdd1History_[i])||
        !readValue(in,ec.sdd1RunCount_[i])||!readValue(in,ec.sdd1RunHasLps_[i])||
        !readValue(in,ec.sdd1RunActive_[i])) {error="Truncated S-DD1 Golomb state.";return false;}
    for(unsigned i=0;i<32u;++i) if(!readValue(in,ec.sdd1ContextState_[i])||
        !readValue(in,ec.sdd1ContextMps_[i])) {error="Truncated S-DD1 context state.";return false;}
    if (!readVector(in, ec.spc7110Regs_, 0x43u) || !readValue(in, ec.spc7110DataPointer_) ||
        !readValue(in, ec.spc7110DecompPointer_) || !readValue(in, ec.spc7110DecompLength_) ||
        !readValue(in, ec.spc7110MathResult_) || !readValue(in, ec.spc7110Remainder_) ||
        !readValue(in, ec.spc7110DecompInitialized_) || !readValue(in, ec.spc7110DecompMode_) ||
        !readValue(in, ec.spc7110DecompOutput_) || !readValue(in, ec.spc7110DecompBufferIndex_) ||
        !readValue(in, ec.spc7110DecompLastPixel_) || !readValue(in, ec.spc7110DecompTop_) ||
        !readValue(in, ec.spc7110DecompValue_) || !readValue(in, ec.spc7110DecompInputBits_) ||
        !readValue(in, ec.spc7110DataPortActive_) || !readValue(in, ec.spc7110OffsetWriteMask_) ||
        !readValue(in, ec.spc7110RtcState_) || !readValue(in, ec.spc7110RtcIndex_) ||
        !readValue(in, ec.spc7110RtcLastUpdate_) || !readValue(in, ec.spc7110RtcClockRemainder_)) {
        error="Truncated SPC7110 state."; return false;
    }
    for(unsigned i=0;i<16u;++i) if(!readValue(in,ec.spc7110PixelOrder_[i])||
        !readValue(in,ec.spc7110RealOrder_[i])||!readValue(in,ec.spc7110BitplaneBuffer_[i])||
        !readValue(in,ec.spc7110RtcRegisters_[i])) {error="Truncated SPC7110 table state.";return false;}
    for(unsigned i=0;i<32u;++i) if(!readValue(in,ec.spc7110ContextIndex_[i])||
        !readValue(in,ec.spc7110ContextInvert_[i])) {error="Truncated SPC7110 context state.";return false;}

    PPU& p=system.ppu_;
    if(!readVector(in,p.frameBuffer_,PPU::HiresWidth*PPU::MaxFrameHeight)||
       !readVector(in,p.layerBuffer_,PPU::HiresWidth*PPU::MaxFrameHeight)||!readVector(in,p.vram_,65536u)||
       !readVector(in,p.cgram_,512u)||!readVector(in,p.oam_,544u)||
       !readVector(in,p.interlaceFrameScratch_,PPU::HiresWidth*PPU::MaxFrameHeight)||
       !readVector(in,p.interlaceLayerScratch_,PPU::HiresWidth*PPU::MaxFrameHeight)) {error="Truncated PPU memory state.";return false;}
    for(unsigned i=0;i<4;++i) {
        if(!readValue(in,p.backgrounds_[i].screenBaseAndSize)||!readValue(in,p.backgrounds_[i].tileBase)||
           !readValue(in,p.backgrounds_[i].hOffset)||!readValue(in,p.backgrounds_[i].vOffset)) {error="Truncated background state.";return false;}
    }
#define RPPU(v) if(!readValue(in,p.v)){error="Truncated PPU register state.";return false;}
    RPPU(outputWidth_); RPPU(outputHeight_); RPPU(renderWidth_);
    RPPU(scanline_); RPPU(dot_); RPPU(masterClock_); RPPU(lineMasterClocks_);
    RPPU(totalScanlines_); RPPU(frameNumber_); RPPU(pal_); RPPU(field_); RPPU(vblank_); RPPU(frameReady_);
    RPPU(hblankStartEvent_); RPPU(hdmaStartEvent_); RPPU(vblankStartEvent_); RPPU(frameStartEvent_); RPPU(refreshStartEvent_);
    RPPU(rangeOver_); RPPU(timeOver_); RPPU(renderFrameEnabled_); RPPU(frameSurfacePrepared_); RPPU(frameHadHires_); RPPU(renderedPixelsThisLine_);
    RPPU(blockedVramAccesses_); RPPU(blockedCgramAccesses_); RPPU(blockedOamAccesses_);
    RPPU(displayControl_); RPPU(objectSelection_); RPPU(bgMode_); RPPU(mosaic_); RPPU(mainScreen_); RPPU(subScreen_);
    RPPU(setIni_); RPPU(vmain_); RPPU(vramAddress_); RPPU(vramReadBuffer_); RPPU(oamReloadAddress_); RPPU(oamAddress_);
    RPPU(oamWriteLatch_); RPPU(oamWriteHigh_); RPPU(oamPriorityRotation_); RPPU(oamFirstSprite_); RPPU(cgramAddress_);
    RPPU(cgramHigh_); RPPU(cgramLatch_); RPPU(scrollLatch_); RPPU(horizontalScrollLatch_);
    RPPU(hCounterLatch_); RPPU(vCounterLatch_); RPPU(hCounterHigh_); RPPU(vCounterHigh_);
    RPPU(mode7Control_); RPPU(mode7A_); RPPU(mode7B_); RPPU(mode7C_); RPPU(mode7D_);
    RPPU(mode7X_); RPPU(mode7Y_); RPPU(mode7HOffset_); RPPU(mode7VOffset_); RPPU(mode7Latch_); RPPU(mode7SecondWrite_);
    RPPU(ppu1OpenBus_); RPPU(ppu2OpenBus_); RPPU(countersLatched_); RPPU(counterLatchEnabled_);
    RPPU(counterLatchPending_); RPPU(pendingCounterH_); RPPU(pendingCounterV_); RPPU(pendingCounterFrame_);
    for(unsigned i=0;i<3;++i) RPPU(windowSelect_[i]);
    for(unsigned i=0;i<2;++i) { RPPU(windowLeft_[i]); RPPU(windowRight_[i]); RPPU(windowLogic_[i]); }
    RPPU(mainWindowMask_); RPPU(subWindowMask_); RPPU(colorMathControl_); RPPU(colorMathEnable_); RPPU(fixedColor_);
#undef RPPU

    APU& a=system.apu_;
    if(!readValue(in,a.cycles_)||!readValue(in,a.executionBudget_)||!readValue(in,a.dspClockRemainder_)||!readValue(in,a.slowCycleRemainder_)){error="Truncated APU state.";return false;}
    in.read(reinterpret_cast<char*>(a.ram_), sizeof(a.ram_));
    for(unsigned i=0;i<4;++i) if(!readValue(in,a.cpuToApu_[i])||!readValue(in,a.apuToCpu_[i])){error="Truncated APU ports.";return false;}
    if(!readValue(in,a.control_)||!readValue(in,a.test_)||!readValue(in,a.dspAddress_)||!readValue(in,a.iplRomEnabled_)){error="Truncated APU registers.";return false;}
    for(unsigned i=0;i<3;++i){APU::Timer& t=a.timers_[i];if(!readValue(in,t.enabled)||!readValue(in,t.target)||!readValue(in,t.divider)||!readValue(in,t.output)||!readValue(in,t.baseCounter)){error="Truncated APU timer state.";return false;}}
    SPC700& spc=a.spc700_;
    if(!readValue(in,spc.pc_)||!readValue(in,spc.a_)||!readValue(in,spc.x_)||!readValue(in,spc.y_)||
       !readValue(in,spc.sp_)||!readValue(in,spc.psw_)||!readValue(in,spc.cycles_)||
       !readValue(in,spc.instructions_)||!readValue(in,spc.stopped_)){error="Truncated SPC state.";return false;}
    DSP& dsp=a.dsp_;
    in.read(reinterpret_cast<char*>(dsp.registers_), sizeof(dsp.registers_));
    if(!readValue(in,dsp.noiseLfsr_)||!readValue(in,dsp.noiseCounter_)||!readValue(in,dsp.rateCounter_)||
       !readValue(in,dsp.echoHistoryIndex_)||!readValue(in,dsp.echoOffset_)){error="Truncated DSP state.";return false;}
    if (version >= 19u) {
        if(!readValue(in,dsp.echoBaseLatched_)||!readValue(in,dsp.echoLengthLatched_)){
            error="Truncated DSP echo latch state.";return false;
        }
    } else {
        // CP27 v18 states have no hidden ESA/EDL latches. Reconstruct them
        // from visible DSP control registers without changing the ROM state.
        dsp.echoBaseLatched_ = static_cast<u16>(dsp.registers_[0x6Du] << 8);
        dsp.echoLengthLatched_ = static_cast<unsigned>(dsp.registers_[0x7Du] & 15u) * 0x800u;
        if (!dsp.echoLengthLatched_) dsp.echoLengthLatched_ = 4u;
    }
    in.read(reinterpret_cast<char*>(dsp.echoHistoryLeft_), sizeof(dsp.echoHistoryLeft_));
    in.read(reinterpret_cast<char*>(dsp.echoHistoryRight_), sizeof(dsp.echoHistoryRight_));
    for(unsigned i=0;i<DSP::VoiceCount;++i){
        DSP::Voice& v=dsp.voices_[i];
#define RVOICE(field) if(!readValue(in,v.field)){error="Truncated DSP voice.";return false;}
        RVOICE(pitch); RVOICE(sourceNumber); RVOICE(leftVolume); RVOICE(rightVolume); RVOICE(keyedOn);
        RVOICE(phase); RVOICE(brrAddress); RVOICE(loopAddress); RVOICE(brrNibble); RVOICE(brrHeader);
        RVOICE(history1); RVOICE(history2); RVOICE(envelope); RVOICE(envelopeState);
        RVOICE(envelopeCounter); RVOICE(ended);
#undef RVOICE
        in.read(reinterpret_cast<char*>(v.decoded), sizeof(v.decoded));
    }
    if(!readVector(in,dsp.pendingSamples_,2u*1024u*1024u)||
       !readValue(in,dsp.lastOutputLeft_)||!readValue(in,dsp.lastOutputRight_)||
       !readValue(in,dsp.recentPeak_)||!readValue(in,dsp.samplesGenerated_)||
       !readValue(in,dsp.nonSilentSamples_)){error="Invalid DSP sample/diagnostic state.";return false;}
    spc.connect(&a);
    dsp.connectRam(a.ram_);

    DMA& dma=system.dma_;
    for(unsigned i=0;i<8;++i){DMA::Channel& ch=dma.channels_[i];
#define RCH(v) if(!readValue(in,ch.v)){error="Truncated DMA state.";return false;}
        RCH(control);RCH(bAddress);RCH(aAddress);RCH(aBank);RCH(size);RCH(indirectBank);RCH(tableAddress);RCH(lineCounter);RCH(unused);
        RCH(indirectAddress);RCH(hdmaLinesRemaining);RCH(hdmaActive);RCH(hdmaRepeat);RCH(hdmaDoTransfer);
#undef RCH
    }
    if(!readValue(in,dma.active_)||!readValue(in,dma.activeChannels_)||!readValue(in,dma.hdmaEnable_)||!readValue(in,dma.bytesTransferred_)){error="Truncated DMA status.";return false;}
    Controller* controllers[] = { &system.controller1_, &system.controller2_, &system.controller3_,
                                  &system.controller4_, &system.controller5_ };
    for(unsigned i=0;i<5u;++i){Controller& c=*controllers[i];
        u8 device=0;
        if(!readValue(in,c.state_)||!readValue(in,c.shift_)||!readValue(in,c.serialPosition_)||
           !readValue(in,c.latched_)||!readValue(in,device)||!readValue(in,c.mouseSensitivity_)||
           !readValue(in,c.mouseLeft_)||!readValue(in,c.mouseRight_)||
           !readValue(in,c.mouseXNegative_)||!readValue(in,c.mouseYNegative_)||
           !readValue(in,c.mouseDeltaX_)||!readValue(in,c.mouseDeltaY_)){error="Truncated controller state.";return false;}
        for(unsigned button=0;button<5u;++button)if(!readValue(in,c.lightGunButtons_[button])){error="Truncated light-gun button state.";return false;}
        if(!readValue(in,c.scopeTurboSwitch_)||!readValue(in,c.scopeTurboReported_)||
           !readValue(in,c.scopeFirePulse_)||!readValue(in,c.scopePausePulse_)||
           !readValue(in,c.scopeFireSerial_)||!readValue(in,c.scopePauseSerial_)){error="Truncated Super Scope state.";return false;}
        for(unsigned gun=0;gun<2u;++gun)
            if(!readValue(in,c.lightGunX_[gun])||!readValue(in,c.lightGunY_[gun])||
               !readValue(in,c.lightGunOffscreen_[gun])){error="Truncated light-gun aim state.";return false;}
        if(!readValue(in,c.justifierLatchedGun_)||!readValue(in,c.justifierNextGun_)){error="Truncated Justifier state.";return false;}
        c.device_=device<=static_cast<u8>(Controller::Disconnected)
            ? static_cast<Controller::Device>(device) : Controller::Disconnected;
    }

    Bus& b=system.bus_;
#define RBUS(v) if(!readValue(in,b.v)){error="Truncated bus state.";return false;}
    RBUS(openBus_);RBUS(nmitimen_);RBUS(nmiFlag_);RBUS(nmiEdgePending_);RBUS(irqFlag_);RBUS(pio_);RBUS(wramAddress_);
    RBUS(multiplicand_);RBUS(dividend_);RBUS(quotient_);RBUS(productOrRemainder_);RBUS(memsel_);
    RBUS(hTimer_);RBUS(vTimer_);RBUS(lastIrqScanline_);RBUS(lastIrqDot_);RBUS(accessMasterCycles_);RBUS(cpuBusAccesses_);
    RBUS(autoJoypadBusyMasterCycles_);RBUS(mathBusyMasterCycles_);RBUS(mathOperation_);
    RBUS(mathShift_);RBUS(mathPartialMasterClocks_);
    for(unsigned i=0;i<4;++i) RBUS(autoJoypadResult_[i]);
    RBUS(multitapPort2_);
    if (version >= 20u) {
        RBUS(irqHCounter_); RBUS(irqVCounter_); RBUS(irqCountdown_); RBUS(irqCompareLevel_); RBUS(irqCpuLine_);
    } else {
        // Legacy v18/v19 positions had only a dot-crossing IRQ flag.
        b.irqHCounter_ = p.masterClock_ >= 10 ? static_cast<unsigned>((p.masterClock_ - 10) / 4) : 0u;
        b.irqVCounter_ = static_cast<unsigned>(p.scanline_);
        b.irqCountdown_ = 0u;
        b.irqCompareLevel_ = false;
        b.irqCpuLine_ = b.irqFlag_;
    }
#undef RBUS
    Scheduler& sch=system.scheduler_;
    if(!readValue(in,sch.masterCycles_)||!readValue(in,sch.framesRun_)||!readValue(in,sch.ppuRemainder_)||!readValue(in,sch.apuRemainder_)){error="Truncated scheduler state.";return false;}
    if (version >= 21u) {
        if(!readValue(in,c.sampledInstructionIrq_)||!readValue(in,b.irqLineOneClockAgo_)||
           !readValue(in,b.irqLineTwoClocksAgo_)){error="Truncated v21 interrupt history.";return false;}
    } else {
        c.sampledInstructionIrq_ = b.irqCpuLine_ && !(c.p_ & CPU65816::IrqDisable);
        b.irqLineOneClockAgo_ = b.irqCpuLine_;
        b.irqLineTwoClocksAgo_ = b.irqCpuLine_;
    }
    if (version >= 22u) {
        if(!readValue(in,c.sampledInstructionNmi_)) {error="Truncated v22 NMI poll sample.";return false;}
    } else {
        // Legacy savestates had no last-instruction NMI sample. Conservatively
        // carry forward an already pending edge on first resume.
        c.sampledInstructionNmi_ = c.nmiPending_;
    }
    if (version >= 23u) {
        if (!readValue(in, sch.postDmaInterruptInhibit_) ||
            !readValue(in, sch.postDmaReleaseMasterCycle_)) {
            error="Truncated v23 post-DMA interrupt phase."; return false;
        }
        if (sch.postDmaReleaseMasterCycle_ > sch.masterCycles_) {
            error="Invalid post-DMA interrupt release clock."; return false;
        }
    } else {
        sch.postDmaInterruptInhibit_ = false;
        sch.postDmaReleaseMasterCycle_ = 0u;
    }
    if (version >= 24u) {
        if (!readValue(in,p.objFetchBlockedCurrent_) ||
            !readValue(in,p.objFetchBlockedPending_)) {
            error="Truncated v24 OBJ fetch slots."; return false;
        }
        if (((p.objFetchBlockedCurrent_ | p.objFetchBlockedPending_) >> 34) != 0ull) {
            error="Invalid OBJ fetch mask."; return false;
        }
    } else {
        // Older save states did not capture the immediately preceding
        // HBlank's fetch outcomes. The display control is a conservative
        // fallback, not a claim to recover unrecorded raster history.
        p.objFetchBlockedCurrent_ = (p.displayControl_ & 0x80u) ? ((1ull<<34)-1ull) : 0ull;
        p.objFetchBlockedPending_ = p.objFetchBlockedCurrent_;
    }
    sch.cpuDmaWriteDuringInstruction_ = false;
    sch.nmiBusPollSample_ = false;
    c.instructionNmiPollEnabled_ = true;
    sch.irqPolledOnWrite_ = false;

    if(!in || in.tellg() != payloadSize){error="The save-state payload has an invalid length.";return false;}
    p.mainPixels_.clear();
    p.subPixels_.clear();
    std::fill(p.tileCache2Valid_.begin(), p.tileCache2Valid_.end(), 0);
    std::fill(p.tileCache4Valid_.begin(), p.tileCache4Valid_.end(), 0);
    std::fill(p.tileCache8Valid_.begin(), p.tileCache8Valid_.end(), 0);
    system.state_=static_cast<System::RunState>(state);
    if(system.state_==System::Empty) system.state_=System::Running;
    return true;
}

} // namespace snes
