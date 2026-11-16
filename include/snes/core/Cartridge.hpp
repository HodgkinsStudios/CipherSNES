#ifndef SNES_CORE_CARTRIDGE_HPP
#define SNES_CORE_CARTRIDGE_HPP

#include <string>
#include <vector>
#include "snes/Types.hpp"
#include "snes/core/Component.hpp"
#include "snes/core/EnhancementChip.hpp"

namespace snes {

class Cartridge : public Component {
    friend class SaveStateManager;
public:
    enum Mapping {
        MappingUnknown,
        MappingLoROM,
        MappingHiROM,
        MappingExHiROM
    };

    enum Enhancement {
        EnhancementNone,
        EnhancementOBC1,
        EnhancementDSP,
        EnhancementSuperFX,
        EnhancementSA1,
        EnhancementCX4,
        EnhancementSDD1,
        EnhancementSPC7110,
        EnhancementSPC7110RTC,
        EnhancementSRTC,
        EnhancementSETA,
        EnhancementBSX,
        EnhancementUnknown
    };

    // User-facing compatibility is deliberately more precise than the old
    // supported/unsupported boolean.  Register-level implementations are
    // useful to developers, but are not the same thing as commercial-game
    // compatibility.
    enum Capability {
        CapabilityUnsupported,
        CapabilityExperimentalBootOnly,
        CapabilityPartialGameplay,
        CapabilityCommercialReady
    };

    struct HeaderInfo {
        HeaderInfo();
        std::string title;
        Mapping mapping;
        u8 mapMode;
        u8 cartridgeType;
        u8 romSizeCode;
        u8 ramSizeCode;
        u8 regionCode;
        u16 checksum;
        u16 complement;
        bool checksumPairValid;
        std::size_t headerOffset;
    };

    Cartridge();
    void reset();
    bool loadFromFile(const std::string& path, std::string& error,
                      const std::string& savePathBase = std::string());
    void unload();

    u8 read(u32 address) const;
    void write(u32 address, u8 value);
    void step(unsigned masterClocks);
    void beginDma(unsigned channel, u32 sourceAddress, unsigned length);
    void endDma(unsigned channel);
    bool enhancementIrqPending() const;
    bool saveRam(std::string& error) const;

    bool loaded() const;
    const std::string& path() const;
    const std::string& fileName() const;
    const std::vector<u8>& rom() const;
    const std::vector<u8>& sram() const;
    const HeaderInfo& header() const;
    bool hadCopierHeader() const;
    bool wasInterleaved() const;
    bool isPal() const;
    bool hasObc1() const;
    Enhancement enhancement() const;
    Capability enhancementCapability() const;
    bool enhancementSupported() const;
    const char* enhancementName() const;
    const char* enhancementCapabilityName() const;
    const char* enhancementImplementationStatus() const;
    u64 enhancementOperations() const;
    unsigned enhancementCpuTraceCount() const;
    const CPU65816::InstructionTrace& enhancementCpuTrace(unsigned index) const;
    unsigned enhancementSuperFxTraceCount() const;
    u32 enhancementSuperFxTraceAddress(unsigned index) const;
    u8 enhancementSuperFxTraceOpcode(unsigned index) const;
    u8 enhancementDiagnosticRegister(unsigned index) const;
    u16 resetVector() const;

private:
    static std::string baseName(const std::string& path);
    HeaderInfo inspectHeader(std::size_t offset, Mapping mapping) const;
    int scoreHeader(const HeaderInfo& header) const;
    std::size_t mirrorRomAddress(std::size_t address) const;
    bool decodeRomAddress(u32 address, std::size_t& romAddress) const;
    bool decodeSramAddress(u32 address, std::size_t& ramAddress) const;
    void loadRamFile();
    std::string ramPath() const;
    void loadRtcFile();
    bool saveRtcFile(std::string& error) const;
    std::string rtcPath() const;
    Enhancement detectEnhancement() const;
    u8 readObc1(std::size_t address) const;
    void writeObc1(std::size_t address, u8 value);

    bool loaded_;
    bool hadCopierHeader_;
    bool wasInterleaved_;
    bool ramDirty_;
    std::string path_;
    std::string savePathBase_;
    std::string fileName_;
    std::vector<u8> rom_;
    std::vector<u8> sram_;
    HeaderInfo header_;
    Enhancement enhancement_;
    bool obc1Enabled_;
    u16 obc1Base_;
    u8 obc1Address_;
    u8 obc1Shift_;
    mutable EnhancementChip enhancementChip_;
};

const char* mappingName(Cartridge::Mapping mapping);

} // namespace snes

#endif
