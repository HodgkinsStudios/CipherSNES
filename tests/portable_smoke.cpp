// CipherSNES v0.2 cross-platform ROM/ZIP smoke verification.
// Generates an original minimal LoROM program; no copyrighted ROM content.
#include <CipherSNES/Core.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
static std::vector<unsigned char> makeRom(){
    std::vector<unsigned char> bytes(32768,0xea);
    // BRA -2 at reset: deterministic CPU loop.
    bytes[0]=0x80;bytes[1]=0xfe;
    const std::string title="CipherSNES Smoke ROM";
    for(std::size_t i=0;i<title.size();++i)bytes[0x7fc0+i]=static_cast<unsigned char>(title[i]);
    bytes[0x7fd5]=0x20; // LoROM mapping
    bytes[0x7fd6]=0;bytes[0x7fd7]=5;bytes[0x7fd8]=0;bytes[0x7fd9]=1;
    bytes[0x7fdc]=0xff;bytes[0x7fdd]=0xff;
    bytes[0x7fde]=0;bytes[0x7fdf]=0;
    bytes[0x7ffc]=0;bytes[0x7ffd]=0x80; // $8000 reset vector
    return bytes;
}
static void makeZip(const fs::path& path,const std::vector<unsigned char>& bytes){
    archive* a=archive_write_new();
    if(!a)throw std::runtime_error("archive_write_new failed");
    archive_write_set_format_zip(a);
    if(archive_write_open_filename(a,path.string().c_str())!=ARCHIVE_OK){
        archive_write_free(a);throw std::runtime_error("ZIP open failed");
    }
    archive_entry* entry=archive_entry_new();
    archive_entry_set_pathname(entry,"original-test.sfc");
    archive_entry_set_filetype(entry,AE_IFREG);
    archive_entry_set_perm(entry,0644);
    archive_entry_set_size(entry,bytes.size());
    const int result=archive_write_header(a,entry);
    if(result!=ARCHIVE_OK){
        archive_entry_free(entry);archive_write_free(a);throw std::runtime_error("ZIP header failed");
    }
    const auto count=archive_write_data(a,bytes.data(),bytes.size());
    archive_entry_free(entry);
    archive_write_close(a);
    archive_write_free(a);
    if(count!=static_cast<la_ssize_t>(bytes.size()))throw std::runtime_error("ZIP write failed");
}
static std::uint64_t render(const fs::path& path,const fs::path& saves){
    snes::Core core;std::string error;
    if(!core.loadRom(path.string(),saves.string(),error))
        throw std::runtime_error("Loading "+path.string()+": "+error);
    std::uint64_t total=0;
    for(int i=0;i<4;++i){
        total+=core.runFrame(500000);
        if(core.faulted())throw std::runtime_error("CPU fault: "+core.faultReason());
    }
    const auto v=core.video();
    if(!core.loaded() || core.frameCount()<4 || total==0 || !v.pixels || v.width<256 || v.height<224)
        throw std::runtime_error("Core frame output missing");
    std::uint64_t hash=1469598103934665603ULL;
    for(std::size_t i=0;i<static_cast<std::size_t>(v.width)*v.height;++i){
        hash^=v.pixels[i];hash*=1099511628211ULL;
    }
    if(!core.save(error))throw std::runtime_error("Save: "+error);
    std::cout<<path.filename().string()<<": "<<core.frameCount()<<" frames, "<<total<<" instructions, hash "<<hash<<"\n";
    return hash;
}
int main(){
    try{
        const fs::path dir=fs::temp_directory_path()/("CipherSNES_portable_smoke_"+std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        struct Cleanup{fs::path dir;~Cleanup(){std::error_code ec;fs::remove_all(dir,ec);}} cleanup{dir};
        const auto bytes=makeRom();
        const auto raw=dir/"original-test.sfc";
        {std::ofstream out(raw,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
        const auto packed=dir/"original-test.zip";makeZip(packed,bytes);
        const auto first=render(raw,dir/"saves");
        const auto second=render(packed,dir/"saves");
        if(first!=second)throw std::runtime_error("Raw and ZIP video hashes differ");
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
