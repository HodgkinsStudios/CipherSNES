#include "snes/core/Cartridge.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

namespace snes {

Cartridge::HeaderInfo::HeaderInfo()
    : title(), mapping(MappingUnknown), mapMode(0), cartridgeType(0),
      romSizeCode(0), ramSizeCode(0), regionCode(0), checksum(0),
      complement(0), checksumPairValid(false), headerOffset(0) {}

Cartridge::Cartridge()
    : loaded_(false), hadCopierHeader_(false), wasInterleaved_(false), ramDirty_(false), path_(), savePathBase_(), fileName_(),
      rom_(), sram_(), header_(), enhancement_(EnhancementNone), obc1Enabled_(false), obc1Base_(0x1C00u),
      obc1Address_(0), obc1Shift_(0), enhancementChip_() {}

void Cartridge::reset() {
    enhancementChip_.rebindRuntime();
    if (obc1Enabled_ && sram_.size() >= 0x2000u) {
        obc1Base_ = (sram_[0x1FF5u] & 1u) ? 0x1800u : 0x1C00u;
        obc1Address_ = static_cast<u8>(sram_[0x1FF6u] & 0x7Fu);
        obc1Shift_ = static_cast<u8>((sram_[0x1FF6u] & 3u) << 1);
    }
}

std::string Cartridge::baseName(const std::string& path) {
    const std::string::size_type slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

Cartridge::HeaderInfo Cartridge::inspectHeader(std::size_t offset, Mapping mapping) const {
    HeaderInfo result;
    result.mapping = mapping;
    result.headerOffset = offset;
    if (offset + 0x40u > rom_.size()) return result;

    for (std::size_t i = 0; i < 21u; ++i) {
        const unsigned char c = rom_[offset + i];
        result.title += (c >= 32 && c <= 126) ? static_cast<char>(c) : ' ';
    }
    while (!result.title.empty() && result.title[result.title.size() - 1] == ' ')
        result.title.erase(result.title.size() - 1);

    result.mapMode = rom_[offset + 0x15u];
    result.cartridgeType = rom_[offset + 0x16u];
    result.romSizeCode = rom_[offset + 0x17u];
    result.ramSizeCode = rom_[offset + 0x18u];
    result.regionCode = rom_[offset + 0x19u];
    result.complement = static_cast<u16>(rom_[offset + 0x1Cu] |
                                         (static_cast<u16>(rom_[offset + 0x1Du]) << 8));
    result.checksum = static_cast<u16>(rom_[offset + 0x1Eu] |
                                       (static_cast<u16>(rom_[offset + 0x1Fu]) << 8));
    result.checksumPairValid = static_cast<u16>(result.checksum ^ result.complement) == 0xFFFFu;
    return result;
}

int Cartridge::scoreHeader(const HeaderInfo& h) const {
    if (h.headerOffset == 0 || h.headerOffset + 0x40u > rom_.size()) return -10000;
    int score = 0;
    if (h.checksumPairValid) score += 16;
    if (!h.title.empty()) score += 2;
    for (std::size_t i = 0; i < h.title.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(h.title[i]);
        if (std::isprint(c)) ++score;
        else score -= 4;
    }

    const u8 mode = static_cast<u8>(h.mapMode & 0x2Fu);
    if (h.mapping == MappingLoROM && (mode == 0x20u || mode == 0x22u || mode == 0x23u)) score += 8;
    if (h.mapping == MappingHiROM && (mode == 0x21u)) score += 8;
    if (h.mapping == MappingExHiROM && (mode == 0x25u)) score += 10;
    if (h.romSizeCode >= 8u && h.romSizeCode <= 0x0Eu) score += 2;
    if (h.ramSizeCode <= 0x0Du) score += 1;
    if (h.regionCode <= 0x14u) score += 1;

    const std::size_t vectorOffset = h.headerOffset + 0x3Cu;
    if (vectorOffset + 1u < rom_.size()) {
        const u16 resetVector = static_cast<u16>(rom_[vectorOffset] | (static_cast<u16>(rom_[vectorOffset + 1u]) << 8));
        if (resetVector >= 0x8000u) score += 8;
        else score -= 8;
    }
    return score;
}

Cartridge::Enhancement Cartridge::detectEnhancement() const {
    const u8 map = static_cast<u8>(header_.mapMode & 0x3Fu);
    const u8 type = header_.cartridgeType;

    // These combinations follow the internal ROM-header conventions used by
    // commercial SNES software.  Detection is intentionally conservative: a
    // known unsupported processor is rejected instead of being mis-mapped as
    // a normal LoROM/HiROM cartridge and producing an unexplained black screen.
    if (type == 0x25u) return EnhancementOBC1;
    if (map == 0x23u || type == 0x34u || type == 0x35u) return EnhancementSA1;
    if (map == 0x22u || type == 0x13u || type == 0x14u || type == 0x15u || type == 0x1Au)
        return EnhancementSuperFX;
    if (map == 0x32u || type == 0x43u || type == 0x45u) return EnhancementSDD1;
    if (type == 0xF5u) return EnhancementSPC7110;
    if (type == 0xF9u) return EnhancementSPC7110RTC;
    if (type == 0x55u) return EnhancementSRTC;
    if (type == 0xF3u) return EnhancementCX4;
    if (type == 0xF6u) return EnhancementSETA;

    // Standard DSP cartridge types occupy the $0x family with a low nibble of
    // three or greater.  This is the same broad header test used by mature
    // emulators before selecting the individual DSP memory map.
    if ((type & 0xF0u) == 0x00u && (type & 0x0Fu) >= 3u) return EnhancementDSP;

    // Broadcast/Satellaview memory packs use non-standard maps and cannot be
    // executed by the normal fixed-ROM mapper.
    if ((header_.mapMode & 0xEFu) == 0x20u && header_.title.find("BS") == 0u)
        return EnhancementBSX;

    // $Fx types are reserved for special hardware.  Known plain carts do not
    // use this range, so fail clearly rather than guessing a map.
    if ((type & 0xF0u) == 0xF0u) return EnhancementUnknown;
    return EnhancementNone;
}

bool Cartridge::loadFromFile(const std::string& path, std::string& error, const std::string& savePathBase) {
    error.clear();
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) {
        error = "Could not open the selected ROM file.";
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff end = input.tellg();
    if (end <= 0) {
        error = "The selected ROM file is empty.";
        return false;
    }
    if (static_cast<unsigned long long>(end) > 128ull * 1024ull * 1024ull) {
        error = "The selected file is larger than the 128 MiB safety limit.";
        return false;
    }
    input.seekg(0, std::ios::beg);

    std::vector<u8> raw(static_cast<std::size_t>(end));
    input.read(reinterpret_cast<char*>(&raw[0]), static_cast<std::streamsize>(raw.size()));
    if (!input) {
        error = "The ROM file could not be read completely.";
        return false;
    }

    hadCopierHeader_ = raw.size() > 512u && (raw.size() % 0x8000u) == 512u;
    wasInterleaved_ = false;
    if (hadCopierHeader_) rom_.assign(raw.begin() + 512, raw.end());
    else rom_.swap(raw);

    HeaderInfo candidates[3];
    candidates[0] = inspectHeader(0x7FC0u, MappingLoROM);
    candidates[1] = inspectHeader(0xFFC0u, MappingHiROM);
    candidates[2] = inspectHeader(0x40FFC0u, MappingExHiROM);

    int bestScore = std::numeric_limits<int>::min();
    int bestIndex = -1;
    for (int i = 0; i < 3; ++i) {
        const int score = scoreHeader(candidates[i]);
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }
    HeaderInfo bestHeader = bestIndex >= 0 ? candidates[bestIndex] : HeaderInfo();

    // Some old SMC/Game Doctor dumps store the two 32 KiB halves of every
    // 64 KiB group in an interleaved order.  Try the standard Type-1 layout
    // only when it produces a substantially more credible internal header.
    if (rom_.size() >= 0x10000u && (rom_.size() % 0x10000u) == 0u && rom_.size() <= 0x1000000u) {
        const std::vector<u8> original = rom_;
        const std::size_t groups = rom_.size() / 0x10000u;
        std::vector<u8> deinterleaved(rom_.size());
        for (std::size_t group = 0; group < groups; ++group) {
            const std::size_t lowerSource = group * 0x8000u;
            const std::size_t upperSource = (groups + group) * 0x8000u;
            std::copy(original.begin() + upperSource, original.begin() + upperSource + 0x8000u,
                      deinterleaved.begin() + group * 0x10000u);
            std::copy(original.begin() + lowerSource, original.begin() + lowerSource + 0x8000u,
                      deinterleaved.begin() + group * 0x10000u + 0x8000u);
        }
        rom_.swap(deinterleaved);
        HeaderInfo interleavedCandidates[3];
        interleavedCandidates[0] = inspectHeader(0x7FC0u, MappingLoROM);
        interleavedCandidates[1] = inspectHeader(0xFFC0u, MappingHiROM);
        interleavedCandidates[2] = inspectHeader(0x40FFC0u, MappingExHiROM);
        int deinterleavedScore = std::numeric_limits<int>::min();
        int deinterleavedIndex = -1;
        for (int i = 0; i < 3; ++i) {
            const int score = scoreHeader(interleavedCandidates[i]);
            if (score > deinterleavedScore) {
                deinterleavedScore = score;
                deinterleavedIndex = i;
            }
        }
        if (deinterleavedIndex >= 0 && deinterleavedScore >= bestScore + 8) {
            bestScore = deinterleavedScore;
            bestHeader = interleavedCandidates[deinterleavedIndex];
            wasInterleaved_ = true;
        } else {
            rom_ = original;
        }
    }

    header_ = bestHeader;
    if (bestScore < 0) header_.mapping = MappingLoROM;

    path_ = path;
    savePathBase_ = savePathBase;
    fileName_ = baseName(path);
    loaded_ = true;
    ramDirty_ = false;
    enhancement_ = detectEnhancement();
    obc1Enabled_ = enhancement_ == EnhancementOBC1;
    EnhancementChip::Kind chipKind = EnhancementChip::None;
    if (enhancement_ == EnhancementDSP) chipKind = EnhancementChip::DSP;
    else if (enhancement_ == EnhancementSuperFX) chipKind = EnhancementChip::SuperFX;
    else if (enhancement_ == EnhancementSA1) chipKind = EnhancementChip::SA1;
    else if (enhancement_ == EnhancementCX4) chipKind = EnhancementChip::CX4;
    else if (enhancement_ == EnhancementSDD1) chipKind = EnhancementChip::SDD1;
    else if (enhancement_ == EnhancementSPC7110) chipKind = EnhancementChip::SPC7110;
    else if (enhancement_ == EnhancementSPC7110RTC) chipKind = EnhancementChip::SPC7110RTC;
    enhancementChip_.configure(chipKind, header_.mapMode, header_.cartridgeType,
                               header_.title, rom_.size(), sram_.size());

    if (!enhancementSupported()) {
        error = std::string("This ROM requires the ") + enhancementName() +
                " cartridge processor, which is not implemented in this build. "
                "Earlier builds treated it as plain LoROM/HiROM and usually displayed a permanent black screen.";
        unload();
        return false;
    }

    const u16 vector = resetVector();
    if (vector < 0x8000u || vector == 0xFFFFu) {
        error = "The selected file does not contain a valid SNES reset vector. It may be interleaved, corrupted, or not an SNES ROM.";
        unload();
        return false;
    }

    std::size_t ramSize = 0;
    if (header_.ramSizeCode != 0 && header_.ramSizeCode < 16)
        ramSize = static_cast<std::size_t>(1024u) << header_.ramSizeCode;
    if (ramSize > 2u * MiB) ramSize = 2u * MiB;
    if (obc1Enabled_ && ramSize < 0x2000u) ramSize = 0x2000u;
    sram_.assign(ramSize, 0xFFu);
    enhancementChip_.configure(chipKind, header_.mapMode, header_.cartridgeType,
                               header_.title, rom_.size(), sram_.size());
    loadRamFile();
    loadRtcFile();
    reset();
    return true;
}

std::size_t Cartridge::mirrorRomAddress(std::size_t address) const {
    if (rom_.empty()) return 0;
    if ((rom_.size() & (rom_.size() - 1u)) == 0u) return address & (rom_.size() - 1u);
    return address % rom_.size();
}

bool Cartridge::decodeRomAddress(u32 address, std::size_t& romAddress) const {
    if (!loaded_ || rom_.empty()) return false;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 offset = static_cast<u16>(address);

    switch (header_.mapping) {
        case MappingHiROM:
            if ((bank >= 0x40u && bank <= 0x7Du) || bank >= 0xC0u) {
                romAddress = (static_cast<std::size_t>(bank & 0x3Fu) << 16) | offset;
                romAddress = mirrorRomAddress(romAddress);
                return true;
            }
            if (((bank <= 0x3Fu) || (bank >= 0x80u && bank <= 0xBFu)) && offset >= 0x8000u) {
                romAddress = (static_cast<std::size_t>(bank & 0x3Fu) << 16) | offset;
                romAddress = mirrorRomAddress(romAddress);
                return true;
            }
            break;
        case MappingExHiROM:
            if (bank >= 0xC0u) {
                romAddress = (static_cast<std::size_t>(bank & 0x3Fu) << 16) | offset;
                romAddress = mirrorRomAddress(romAddress);
                return true;
            }
            if (bank >= 0x40u && bank <= 0x7Du) {
                romAddress = 0x400000u + (static_cast<std::size_t>(bank & 0x3Fu) << 16) + offset;
                romAddress = mirrorRomAddress(romAddress);
                return true;
            }
            if (((bank <= 0x3Fu) || (bank >= 0x80u && bank <= 0xBFu)) && offset >= 0x8000u) {
                romAddress = (static_cast<std::size_t>(bank & 0x3Fu) << 16) | offset;
                romAddress = mirrorRomAddress(romAddress);
                return true;
            }
            break;
        case MappingUnknown:
        case MappingLoROM:
        default:
            if (offset >= 0x8000u || (bank >= 0x40u && bank <= 0x7Du) || bank >= 0xC0u) {
                romAddress = (static_cast<std::size_t>(bank & 0x7Fu) << 15) | (offset & 0x7FFFu);
                romAddress = mirrorRomAddress(romAddress);
                return true;
            }
            break;
    }
    return false;
}

bool Cartridge::decodeSramAddress(u32 address, std::size_t& ramAddress) const {
    // SA-1 SRAM is exclusively decoded by the enhancement chip's shared
    // BW-RAM windows. Generic LoROM $70-$7D/$F0-$FF SRAM must not alias it.
    if (enhancement_ == EnhancementSA1 || sram_.empty()) return false;
    const u8 bank = static_cast<u8>(address >> 16);
    const u16 offset = static_cast<u16>(address);

    if (header_.mapping == MappingHiROM || header_.mapping == MappingExHiROM) {
        if (((bank >= 0x20u && bank <= 0x3Fu) || (bank >= 0xA0u && bank <= 0xBFu)) &&
            offset >= 0x6000u && offset <= 0x7FFFu) {
            ramAddress = (static_cast<std::size_t>(bank & 0x1Fu) << 13) | (offset & 0x1FFFu);
            ramAddress %= sram_.size();
            return true;
        }
    } else {
        if (((bank >= 0x70u && bank <= 0x7Du) || bank >= 0xF0u) && offset <= 0x7FFFu) {
            ramAddress = (static_cast<std::size_t>(bank & 0x0Fu) << 15) | offset;
            ramAddress %= sram_.size();
            return true;
        }
        if (((bank <= 0x3Fu) || (bank >= 0x80u && bank <= 0xBFu)) &&
            offset >= 0x6000u && offset <= 0x7FFFu) {
            ramAddress = (static_cast<std::size_t>(bank & 0x0Fu) << 13) | (offset & 0x1FFFu);
            ramAddress %= sram_.size();
            return true;
        }
    }
    return false;
}

u8 Cartridge::readObc1(std::size_t address) const {
    address &= 0x1FFFu;
    if (address >= 0x1FF0u && address <= 0x1FF3u)
        return sram_[(obc1Base_ + static_cast<u16>(obc1Address_) * 4u + (address - 0x1FF0u)) & 0x1FFFu];
    if (address == 0x1FF4u)
        return sram_[(obc1Base_ + (obc1Address_ >> 2) + 0x200u) & 0x1FFFu];
    return sram_[address];
}

void Cartridge::writeObc1(std::size_t address, u8 value) {
    address &= 0x1FFFu;
    std::size_t target = address;
    if (address >= 0x1FF0u && address <= 0x1FF3u) {
        target = (obc1Base_ + static_cast<u16>(obc1Address_) * 4u + (address - 0x1FF0u)) & 0x1FFFu;
    } else if (address == 0x1FF4u) {
        target = (obc1Base_ + (obc1Address_ >> 2) + 0x200u) & 0x1FFFu;
        const u8 mask = static_cast<u8>(3u << obc1Shift_);
        value = static_cast<u8>((sram_[target] & ~mask) | ((value & 3u) << obc1Shift_));
    } else if (address == 0x1FF5u) {
        obc1Base_ = (value & 1u) ? 0x1800u : 0x1C00u;
    } else if (address == 0x1FF6u) {
        obc1Address_ = static_cast<u8>(value & 0x7Fu);
        obc1Shift_ = static_cast<u8>((value & 3u) << 1);
    }
    if (sram_[target] != value) {
        sram_[target] = value;
        ramDirty_ = true;
    }
}

u8 Cartridge::read(u32 address) const {
    u8 chipValue = 0xFFu;
    if (enhancementChip_.read(address, rom_, const_cast<std::vector<u8>&>(sram_), chipValue))
        return chipValue;
    std::size_t decoded = 0;
    if (decodeSramAddress(address, decoded)) return obc1Enabled_ ? readObc1(decoded) : sram_[decoded];
    if (enhancementChip_.mapRomAddress(address, rom_.size(), decoded)) return rom_[decoded];
    if (decodeRomAddress(address, decoded)) return rom_[decoded];
    return 0xFFu;
}

void Cartridge::write(u32 address, u8 value) {
    if (enhancementChip_.write(address, value, rom_, sram_, ramDirty_)) return;
    std::size_t decoded = 0;
    if (decodeSramAddress(address, decoded)) {
        if (obc1Enabled_) { writeObc1(decoded, value); return; }
        if (sram_[decoded] != value) {
            sram_[decoded] = value;
            ramDirty_ = true;
        }
    }
}

void Cartridge::step(unsigned masterClocks) {
    enhancementChip_.step(masterClocks, rom_, sram_, ramDirty_);
}

void Cartridge::beginDma(unsigned channel,u32 sourceAddress,unsigned length){
    enhancementChip_.beginDma(channel,sourceAddress,length);
}

void Cartridge::endDma(unsigned channel){enhancementChip_.endDma(channel);}
bool Cartridge::enhancementIrqPending() const{return enhancementChip_.irqPending();}
unsigned Cartridge::enhancementCpuTraceCount() const{return enhancementChip_.secondaryCpuTraceCount();}
const CPU65816::InstructionTrace& Cartridge::enhancementCpuTrace(unsigned index) const{return enhancementChip_.secondaryCpuTrace(index);}
unsigned Cartridge::enhancementSuperFxTraceCount() const{return enhancementChip_.superFxTraceCount();}
u32 Cartridge::enhancementSuperFxTraceAddress(unsigned index) const{return enhancementChip_.superFxTraceAddress(index);}
u8 Cartridge::enhancementSuperFxTraceOpcode(unsigned index) const{return enhancementChip_.superFxTraceOpcode(index);}
u8 Cartridge::enhancementDiagnosticRegister(unsigned index) const{return enhancementChip_.diagnosticRegister(index);}

std::string Cartridge::ramPath() const {
    if (!savePathBase_.empty()) return savePathBase_ + ".srm";
    if (path_.empty()) return std::string();
    const std::string::size_type dot = path_.find_last_of('.');
    const std::string::size_type slash = path_.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return path_.substr(0, dot) + ".srm";
    return path_ + ".srm";
}

std::string Cartridge::rtcPath() const{
    if (!savePathBase_.empty()) return savePathBase_ + ".rtc";
    if(path_.empty())return std::string();
    const std::string::size_type dot=path_.find_last_of('.');
    const std::string::size_type slash=path_.find_last_of("/\\");
    if(dot!=std::string::npos&&(slash==std::string::npos||dot>slash))return path_.substr(0,dot)+".rtc";
    return path_+".rtc";
}

void Cartridge::loadRamFile() {
    if (sram_.empty()) return;
    std::ifstream input(ramPath().c_str(), std::ios::binary);
    if (!input) return;
    input.read(reinterpret_cast<char*>(&sram_[0]), static_cast<std::streamsize>(sram_.size()));
    ramDirty_ = false;
}

void Cartridge::loadRtcFile(){
    std::vector<u8> state;
    if(!enhancementChip_.exportRtcState(state))return;
    std::ifstream input(rtcPath().c_str(),std::ios::binary);
    if(!input)return;
    char magic[8];input.read(magic,8);
    if(!input||std::memcmp(magic,"S711RTC1",8)!=0)return;
    state.assign(24u,0);input.read(reinterpret_cast<char*>(&state[0]),24);
    if(input)enhancementChip_.importRtcState(state);
}

bool Cartridge::saveRtcFile(std::string& error) const{
    std::vector<u8> state;
    if(!enhancementChip_.exportRtcState(state))return true;
    std::ofstream output(rtcPath().c_str(),std::ios::binary|std::ios::trunc);
    if(!output){error="Could not create the SPC7110 RTC state file.";return false;}
    output.write("S711RTC1",8);output.write(reinterpret_cast<const char*>(&state[0]),static_cast<std::streamsize>(state.size()));
    if(!output){error="Could not write the complete SPC7110 RTC state file.";return false;}
    return true;
}

bool Cartridge::saveRam(std::string& error) const {
    error.clear();
    if(!loaded_)return true;
    if(!saveRtcFile(error))return false;
    if(sram_.empty())return true;
    std::ofstream output(ramPath().c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not create the cartridge save-RAM file.";
        return false;
    }
    output.write(reinterpret_cast<const char*>(&sram_[0]), static_cast<std::streamsize>(sram_.size()));
    if (!output) {
        error = "Could not write the complete cartridge save-RAM file.";
        return false;
    }
    return true;
}

void Cartridge::unload() {
    if (loaded_ && (ramDirty_ || enhancement_==EnhancementSPC7110RTC)) {
        std::string ignored;
        saveRam(ignored);
    }
    loaded_ = false;
    hadCopierHeader_ = false;
    wasInterleaved_ = false;
    ramDirty_ = false;
    path_.clear();
    savePathBase_.clear();
    fileName_.clear();
    rom_.clear();
    sram_.clear();
    header_ = HeaderInfo();
    enhancement_ = EnhancementNone;
    obc1Enabled_ = false;
    obc1Base_ = 0x1C00u;
    obc1Address_ = 0;
    obc1Shift_ = 0;
    enhancementChip_.configure(EnhancementChip::None, 0, 0, std::string(), 0, 0);
}

bool Cartridge::loaded() const { return loaded_; }
const std::string& Cartridge::path() const { return path_; }
const std::string& Cartridge::fileName() const { return fileName_; }
const std::vector<u8>& Cartridge::rom() const { return rom_; }
const std::vector<u8>& Cartridge::sram() const { return sram_; }
const Cartridge::HeaderInfo& Cartridge::header() const { return header_; }
bool Cartridge::hadCopierHeader() const { return hadCopierHeader_; }
bool Cartridge::wasInterleaved() const { return wasInterleaved_; }
bool Cartridge::hasObc1() const { return obc1Enabled_; }
Cartridge::Enhancement Cartridge::enhancement() const { return enhancement_; }
Cartridge::Capability Cartridge::enhancementCapability() const {
    switch (enhancement_) {
        case EnhancementNone:
        case EnhancementOBC1:
            return CapabilityCommercialReady;
        case EnhancementDSP:
        case EnhancementCX4:
            return CapabilityPartialGameplay;
        case EnhancementSuperFX:
        case EnhancementSA1:
            return CapabilityExperimentalBootOnly;
        case EnhancementSDD1:
        case EnhancementSPC7110:
        case EnhancementSPC7110RTC:
            return CapabilityPartialGameplay;
        case EnhancementSRTC:
        case EnhancementSETA:
        case EnhancementBSX:
        case EnhancementUnknown:
        default:
            return CapabilityUnsupported;
    }
}
bool Cartridge::enhancementSupported() const {
    // Kept for source compatibility with older callers.  New code should use
    // enhancementCapability() so experimental register paths are not mistaken
    // for completed commercial support.
    return enhancementCapability() != CapabilityUnsupported;
}
const char* Cartridge::enhancementName() const {
    switch (enhancement_) {
        case EnhancementNone: return "None";
        case EnhancementOBC1: return "OBC-1";
        case EnhancementDSP: return "DSP-1/2/3/4";
        case EnhancementSuperFX: return "Super FX";
        case EnhancementSA1: return "SA-1";
        case EnhancementCX4: return "CX4";
        case EnhancementSDD1: return "S-DD1";
        case EnhancementSPC7110: return "SPC7110";
        case EnhancementSPC7110RTC: return "SPC7110 + RTC";
        case EnhancementSRTC: return "S-RTC";
        case EnhancementSETA: return "ST010/011/018";
        case EnhancementBSX: return "BS-X/Satellaview";
        default: return "unknown enhancement hardware";
    }
}

const char* Cartridge::enhancementCapabilityName() const {
    switch (enhancementCapability()) {
        case CapabilityCommercialReady: return "CommercialReady";
        case CapabilityPartialGameplay: return "PartialGameplay";
        case CapabilityExperimentalBootOnly: return "ExperimentalBootOnly";
        default: return "Unsupported";
    }
}

const char* Cartridge::enhancementImplementationStatus() const {
    if (enhancement_ == EnhancementOBC1) return "OBC-1 complete register/RAM mapping";
    return enhancementChip_.implementationStatus();
}

u64 Cartridge::enhancementOperations() const { return enhancementChip_.operations(); }

u16 Cartridge::resetVector() const {
    if (!loaded_) return 0;
    const u8 low = read(0x00FFFCu);
    const u8 high = read(0x00FFFDu);
    return static_cast<u16>(low | (static_cast<u16>(high) << 8));
}

bool Cartridge::isPal() const {
    // Standard destination codes 2 through 12 are PAL territories. Korea
    // and the remaining common codes use NTSC timing.
    return header_.regionCode >= 2u && header_.regionCode <= 12u;
}

const char* mappingName(Cartridge::Mapping mapping) {
    switch (mapping) {
        case Cartridge::MappingLoROM: return "LoROM";
        case Cartridge::MappingHiROM: return "HiROM";
        case Cartridge::MappingExHiROM: return "ExHiROM";
        default: return "Unknown";
    }
}

} // namespace snes
