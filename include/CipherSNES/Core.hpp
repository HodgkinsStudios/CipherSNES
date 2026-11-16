// Created by Jacob Hodgkins
// CipherSNES frontend-independent public C++17 interface, v1.
#pragma once
#include <CipherSNES/Buttons.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace snes {
struct CoreVideo {
    const std::uint32_t* pixels=nullptr; // XRGB8888; valid until next frame/reset/load
    unsigned width=0,height=0,pitchBytes=0;
};
struct CoreCartridgeInfo {
    std::string title;
    std::string mapping;
    std::size_t romSize=0,ramSize=0;
    std::uint16_t resetVector=0;
    bool checksumValid=false,copierHeaderRemoved=false;
};
class Core final {
public:
    static constexpr unsigned AudioSampleRate=32000;
    static constexpr unsigned AudioChannels=2;
    Core();
    ~Core();
    Core(const Core&)=delete;
    Core& operator=(const Core&)=delete;
    Core(Core&&)=delete;
    Core& operator=(Core&&)=delete;
    // Data, timing, and input are owned by this library. No GTK/SDL dependencies.
    // Both raw SNES files and .zip ROMs are accepted.
    bool loadRom(const std::string& path,const std::string& saveDirectory,std::string& error);
    bool loadIplFile(const std::string& path,std::string& error);
    bool reset(std::string& error);
    bool save(std::string& error) const;
    bool loaded() const;
    CoreCartridgeInfo cartridgeInfo() const;
    void setButton(unsigned port,SnesButton button,bool pressed);
    std::size_t runFrame(std::size_t instructionLimit=250000);
    // CP53: present only the final frame of a bounded catch-up batch.
    // The legacy signature and its ABI remain unchanged.
    std::size_t runFrame(std::size_t instructionLimit, bool renderVideo);
    CoreVideo video() const;
    std::vector<std::int16_t> takeAudio(); // interleaved signed 16-bit stereo
    void takeAudioInto(std::vector<std::int16_t>& output); // reuse frontend buffer
    void discardAudio();
    bool faulted() const;
    bool stopped() const;
    std::string faultReason() const;
    bool isPal() const;
    std::uint64_t frameCount() const;
    std::uint64_t masterClocks() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
