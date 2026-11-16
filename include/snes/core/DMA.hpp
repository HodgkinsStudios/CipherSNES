#ifndef SNES_CORE_DMA_HPP
#define SNES_CORE_DMA_HPP

#include "snes/Types.hpp"
#include "snes/core/Component.hpp"

namespace snes {

class Bus;

class DMA : public Component {
    friend class SaveStateManager;
public:
    struct Channel {
        Channel();
        u8 control;
        u8 bAddress;
        u16 aAddress;
        u8 aBank;
        u16 size;
        u8 indirectBank;
        u16 tableAddress;
        u8 lineCounter;
        u8 unused;

        // Internal HDMA execution state. These values are deliberately kept
        // separate from the programmer-visible registers so save states can
        // resume part-way through a frame without rebuilding a channel.
        u16 indirectAddress;
        u16 hdmaLinesRemaining;
        bool hdmaActive;
        bool hdmaRepeat;
        bool hdmaDoTransfer;
    };

    DMA();
    void connectBus(Bus* bus);
    void reset();
    void start(u8 channels);
    void setHdmaEnable(u8 channels);
    void initializeHdma();
    void runHdmaScanline();
    u8 readRegister(u16 address) const;
    void writeRegister(u16 address, u8 value);
    bool active() const;
    u8 hdmaEnable() const;
    u64 bytesTransferred() const;

private:
    void transferChannel(unsigned index);
    bool reloadHdmaLineCounter(Channel& channel);
    void transferHdmaChannel(unsigned index, Channel& channel);
    void transferByte(u32 aAddress, u32 bAddress, bool reverse);
    static bool validAAddress(u32 address);
    static bool wramPortConflict(u32 aAddress, u32 bAddress);
    static unsigned bOffset(u8 mode, unsigned index);
    static unsigned transferLength(u8 mode);

    Bus* bus_;
    Channel channels_[8];
    bool active_;
    u8 activeChannels_;
    u8 hdmaEnable_;
    u64 bytesTransferred_;
};

} // namespace snes

#endif
