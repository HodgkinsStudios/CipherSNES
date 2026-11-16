// Created by Jacob Hodgkins — CipherSNES CP15 adapter.
// The complete machine below is the imported OLDSNES_EMU v21 snes::System.
// Only the stable public CipherSNES API is exposed to the launcher.
#include <CipherSNES/Core.hpp>
#include <snes/core/System.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace fs = std::filesystem;
namespace snes {
namespace {
struct TempRom {
    std::string path;
    ~TempRom(){if(!path.empty())std::remove(path.c_str());}
    TempRom(const TempRom&)=delete;
    TempRom& operator=(const TempRom&)=delete;
    TempRom()=default;
};
bool suffix(const std::string& name, const std::string& ext) {
    if(name.size()<ext.size())return false;
    auto start=name.size()-ext.size();
    for(std::size_t i=0;i<ext.size();++i)
        if(std::tolower(static_cast<unsigned char>(name[start+i]))!=ext[i])return false;
    return true;
}
bool extractZip(const std::string& path,TempRom& temp,std::string& error){
    archive* a=archive_read_new();
    if(!a){error="Cannot initialize ZIP reader";return false;}
    archive_read_support_filter_all(a);
    archive_read_support_format_zip(a);
    if(archive_read_open_filename(a,path.c_str(),8192)!=ARCHIVE_OK){
        error=archive_error_string(a)?archive_error_string(a):"Cannot open ZIP";
        archive_read_free(a);return false;
    }
    bool found=false;
    struct archive_entry* entry=nullptr;
    while(archive_read_next_header(a,&entry)==ARCHIVE_OK){
        const char* name=archive_entry_pathname(entry);
        const std::string member=name?name:"";
        if(archive_entry_filetype(entry)!=AE_IFREG ||
           !(suffix(member,".sfc")||suffix(member,".smc")||suffix(member,".fig")||suffix(member,".swc"))){
            archive_read_data_skip(a);continue;
        }
        if(archive_entry_size(entry)<0 || archive_entry_size(entry)>128ll*1024*1024){
            error="ZIP ROM exceeds the 128 MiB safety limit";break;
        }
        // Use the host temporary directory, not a hard-coded Unix /tmp path.
        // Both APIs create a unique file atomically, before decompression.
#if defined(_WIN32)
        char directory[MAX_PATH + 1]{};
        char temporary[MAX_PATH + 1]{};
        const DWORD length = GetTempPathA(MAX_PATH, directory);
        if(length == 0 || length >= MAX_PATH ||
           !GetTempFileNameA(directory, "CSN", 0, temporary)) {
            error="Unable to create temporary ROM";break;
        }
        temp.path=temporary;
#else
        std::error_code ec;
        const auto directory=fs::temp_directory_path(ec);
        if(ec){error="Unable to locate temporary directory";break;}
        const std::string pattern=(directory/"CipherSNES_ROM_XXXXXX").string();
        std::vector<char> temporary(pattern.begin(),pattern.end());
        temporary.push_back('\0');
        const int fd=mkstemp(temporary.data());
        if(fd<0){error="Unable to create temporary ROM";break;}
        temp.path=temporary.data();
        close(fd);
#endif
        std::ofstream out(temp.path,std::ios::binary|std::ios::trunc);
        if(!out){error="Unable to write temporary ROM";break;}
        std::size_t total=0;
        char buf[16384];
        bool ok=true;
        while(true){
            la_ssize_t n=archive_read_data(a,buf,sizeof(buf));
            if(n==0)break;
            if(n<0){error="Corrupted ZIP ROM entry";ok=false;break;}
            total+=static_cast<std::size_t>(n);
            if(total>128ull*1024*1024){error="Expanded ROM exceeds 128 MiB";ok=false;break;}
            out.write(buf,n);
            if(!out){error="Temporary ROM write failed";ok=false;break;}
        }
        out.close();
        if(ok && total>=32768)found=true;
        else if(ok)error="ROM entry is too small";
        break;
    }
    archive_read_close(a);archive_read_free(a);
    if(!found && error.empty())error="ZIP contains no usable SNES ROM (.sfc/.smc/.fig/.swc)";
    return found;
}
std::string coreMappingName(Cartridge::Mapping value){
    switch(value){case Cartridge::MappingLoROM:return "LoROM";
        case Cartridge::MappingHiROM:return "HiROM";
        case Cartridge::MappingExHiROM:return "ExHiROM";
        default:return "Unknown";}
}
} // namespace
struct Core::Impl {
    System system;
    std::string saveBase;
    std::vector<std::uint8_t> optionalIpl;
};
Core::Core():impl_(std::make_unique<Impl>()){}
Core::~Core(){std::string ignored;save(ignored);}
bool Core::save(std::string& error) const {
    error.clear();
    return !impl_->system.cartridge().loaded()||impl_->system.saveBatteryRam(error);
}
bool Core::loadRom(const std::string& path,const std::string& saveDirectory,std::string& error){
    error.clear();
    TempRom temporary;
    std::string romPath=path;
    if(suffix(path,".zip")){
        if(!extractZip(path,temporary,error))return false;
        romPath=temporary.path;
    }
    try {
        const fs::path saveDir=saveDirectory.empty()?fs::path(path).parent_path():fs::path(saveDirectory);
        if(!saveDir.empty())fs::create_directories(saveDir);
        const auto stem=fs::path(path).stem().string();
        const auto basename=(saveDir/fs::path(stem)).string();
        if(!save(error))return false;
        // Set save override BEFORE SRAM/RTC are loaded, not just when saving.
        if(!impl_->system.loadCartridge(romPath,error,true,basename))return false;
        impl_->saveBase=basename;
        if(!impl_->optionalIpl.empty())impl_->system.apu().loadIpl(impl_->optionalIpl);
        return true;
    } catch(const fs::filesystem_error& ex){error=ex.what();return false;}
}
bool Core::loadIplFile(const std::string& path,std::string& error){
    error.clear();
    std::ifstream input(path,std::ios::binary);
    if(!input){error="Unable to open SPC700 IPL: "+path;return false;}
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    if(bytes.size()!=64u){error="SPC700 IPL must be exactly 64 bytes";return false;}
    if(!impl_->system.apu().loadIpl(bytes)){error="SPC700 IPL rejected";return false;}
    impl_->optionalIpl=std::move(bytes);return true;
}
bool Core::reset(std::string& error){
    if(!save(error))return false;
    impl_->system.reset();
    if(!impl_->optionalIpl.empty())impl_->system.apu().loadIpl(impl_->optionalIpl);
    error.clear();return true;
}
bool Core::loaded() const{return impl_->system.cartridge().loaded();}
CoreCartridgeInfo Core::cartridgeInfo() const{
    const auto& c=impl_->system.cartridge();
    if(!c.loaded())return {};
    const auto& header=c.header();
    return {header.title,coreMappingName(header.mapping),c.rom().size(),c.sram().size(),
        c.resetVector(),header.checksumPairValid,c.hadCopierHeader()};
}
void Core::setButton(unsigned port,SnesButton button,bool pressed){
    if(port<2 && static_cast<unsigned>(button)<buttonCount)
       impl_->system.setControllerButton(port,static_cast<Controller::Button>(button),pressed);
}
std::size_t Core::runFrame(std::size_t budget){
    return runFrame(budget, true);
}
std::size_t Core::runFrame(std::size_t budget, bool renderVideo){
    if(!loaded() || impl_->system.state()!=System::Running)return 0;
    const std::uint64_t before=impl_->system.cpu().instructions();
    const unsigned limit=static_cast<unsigned>(std::min<std::size_t>(budget,1000000u));
    impl_->system.runFrame(renderVideo,limit);
    const std::uint64_t after=impl_->system.cpu().instructions();
    return static_cast<std::size_t>(after-before);
}
CoreVideo Core::video() const {
    if(!loaded())return {};
    const PPU& ppu=impl_->system.ppu();
    const auto& pixels=ppu.frameBuffer();
    if(pixels.empty())return {};
    const unsigned width=static_cast<unsigned>(ppu.outputWidth());
    const unsigned height=static_cast<unsigned>(ppu.outputHeight());
    return {pixels.data(),width,height,width*4u};
}
std::vector<std::int16_t> Core::takeAudio(){
    const auto& src=impl_->system.pendingAudioSamples();
    std::vector<std::int16_t> output(src.begin(),src.end());
    impl_->system.clearPendingAudioSamples();return output;
}
void Core::takeAudioInto(std::vector<std::int16_t>& output){
    const auto& samples=impl_->system.pendingAudioSamples();
    output.assign(samples.begin(),samples.end());
    impl_->system.clearPendingAudioSamples();
}
void Core::discardAudio(){impl_->system.clearPendingAudioSamples();}
bool Core::faulted() const{return impl_->system.state()==System::Faulted;}
bool Core::stopped() const{return impl_->system.cpu().stopped();}
std::string Core::faultReason() const{
    if(faulted())return "Old SNES core: execution stopped before completion of frame";
    if(stopped())return "65C816 STP instruction";
    return {};
}
bool Core::isPal() const {return impl_->system.ppu().isPal();}
std::uint64_t Core::frameCount() const{return impl_->system.scheduler().framesRun();}
std::uint64_t Core::masterClocks() const{return impl_->system.scheduler().masterCycles();}
} // namespace snes
