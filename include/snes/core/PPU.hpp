#ifndef SNES_CORE_PPU_HPP
#define SNES_CORE_PPU_HPP

#include <vector>
#include "snes/Types.hpp"
#include "snes/core/Component.hpp"

namespace snes {

class PPU : public Component {
    friend class SaveStateManager;
public:
    enum {
        FrameWidth = 256,
        FrameHeight = 224,
        HiresWidth = 512,
        OverscanHeight = 239,
        InterlaceFrameHeight = 448,
        InterlaceOverscanHeight = 478,
        MaxFrameHeight = InterlaceOverscanHeight
    };

    PPU();
    void reset();
    void stepDot();
    void stepDots(unsigned dots);
    void stepMasterClocks(unsigned clocks);
    void setPal(bool pal);
    void latchCounters(int horizontal, int vertical);
    void latchCountersFromWrioFallingEdge();
    void scheduleCounterLatch(int horizontal, int vertical);
    void setCounterLatchEnabled(bool enabled);

    void writeRegister(u16 address, u8 value);
    u8 readRegister(u16 address);

    const std::vector<u32>& frameBuffer() const;
    const std::vector<u8>& vram() const;
    const std::vector<u8>& cgram() const;
    const std::vector<u8>& oam() const;
    int outputWidth() const;
    int outputHeight() const;
    int scanline() const;
    int dot() const;
    int masterClock() const;
    int lineMasterClocks() const;
    int totalScanlines() const;
    int visibleLineCount() const;
    double frameRate() const;
    bool isPal() const;
    u64 frameNumber() const;
    bool inVBlank() const;
    bool inHBlank() const;
    bool field() const;
    bool consumeHBlankStart();
    bool consumeHdmaStart();
    bool consumeVBlankStart();
    bool consumeFrameStart();
    bool consumeRefreshStart();
    bool forcedBlank() const;
    u8 brightness() const;
    u8 mainScreenMask() const;
    u8 subScreenMask() const;
    u8 colorMathControl() const;
    u8 colorMathEnable() const;
    u8 setIni() const;
    u16 backdropColor() const;
    u16 fixedColor() const;
    u8 backgroundMode() const;
    u16 backgroundHorizontalOffset(unsigned index) const;
    u16 backgroundVerticalOffset(unsigned index) const;
    u64 blockedVramAccesses() const;
    u64 blockedCgramAccesses() const;
    u64 blockedOamAccesses() const;
    void setFrameRenderingEnabled(bool enabled);
    bool consumeFrameReady();

private:
    struct Background {
        Background() : screenBaseAndSize(0), tileBase(0), hOffset(0), vOffset(0) {}
        u8 screenBaseAndSize;
        u16 tileBase;
        u16 hOffset;
        u16 vOffset;
    };

    struct Pixel {
        Pixel() : color(0), priority(-32768), layer(5), opaque(false),
                  colorMathEligible(true), backdrop(false) {}
        u16 color;
        int priority;
        u8 layer;
        bool opaque;
        bool colorMathEligible;
        // True only for the transparent/backdrop result left after layer
        // selection.  This is required for the SNES half-colour exception:
        // when CGWSEL selects the sub screen, halving is suppressed if the
        // selected sub pixel is the fixed-colour backdrop rather than a real
        // BG/OBJ pixel.
        bool backdrop;
    };

    void beginFrameSurface();
    void renderScanline(unsigned visibleY);
    void renderScanlineSegment(unsigned visibleY, unsigned logicalStart, unsigned logicalEnd);
    void flushActiveDisplayToCurrentPosition();
    void ensureFrameGeometry(int desiredWidth, int desiredHeight);
    void advanceScanline();
    int currentLineMasterClocks() const;
    void updateDotFromMasterClock();
    void rebuildColorLut();
    void invalidateTileCache(std::size_t byteAddress);
    void decodeTile(unsigned bpp, std::size_t tileAddress) const;
    u32 colorFromCgram(unsigned index) const;
    u16 rawColorFromCgram(unsigned index) const;
    u32 colorFrom15Bit(u16 raw) const;
    u16 directColor(unsigned palette, unsigned pixel) const;
    u16 applyMath15(u16 mainColor, u16 subColor, bool subtract, bool halve) const;
    unsigned tilePixel(unsigned bpp, std::size_t tileAddress, unsigned x, unsigned y) const;
    u16 tilemapEntry(const Background& bg, unsigned tileX, unsigned tileY) const;
    void drawBackground(unsigned bgIndex, unsigned bpp, bool mode0Palette,
                        bool subScreen, int lowPriority, int highPriority,
                        bool allowDirectColor, unsigned screenY,
                        unsigned sourceStart, unsigned sourceEnd);
    void drawSprites(bool subScreen, unsigned screenY,
                     unsigned sourceStart, unsigned sourceEnd);
    void drawMode7(bool subScreen, unsigned screenY,
                   unsigned sourceStart, unsigned sourceEnd);
    void composeScanline(unsigned screenY, unsigned logicalStart, unsigned logicalEnd);
    void incrementVramAddress(bool highPort);
    void prefetchVramReadBuffer();
    u16 translatedVramAddress() const;
    unsigned spriteWidth(bool large) const;
    unsigned spriteHeight(bool large) const;
    bool windowMasksPixel(unsigned layer, unsigned x, bool subScreen) const;
    bool windowContains(unsigned window, unsigned x) const;
    bool windowRuleApplies(unsigned mode, bool inWindow) const;
    void putPixel(std::vector<Pixel>& target, unsigned x, unsigned y,
                  u16 color, int priority, u8 layer,
                  bool colorMathEligible = true);
    void updateSpriteOverflowLine(unsigned screenY);
    bool videoMemoryAccessible() const;

    std::vector<u32> frameBuffer_;
    std::vector<u8> layerBuffer_;
    std::vector<Pixel> mainPixels_;
    std::vector<Pixel> subPixels_;
    std::vector<u8> vram_;
    std::vector<u8> cgram_;
    std::vector<u8> oam_;
    std::vector<u32> interlaceFrameScratch_;
    std::vector<u8> interlaceLayerScratch_;
    mutable std::vector<u8> tileCache2_;
    mutable std::vector<u8> tileCache4_;
    mutable std::vector<u8> tileCache8_;
    mutable std::vector<u8> tileCache2Valid_;
    mutable std::vector<u8> tileCache4Valid_;
    mutable std::vector<u8> tileCache8Valid_;
    std::vector<u32> colorLut_;
    Background backgrounds_[4];

    int outputWidth_;
    int outputHeight_;
    int renderWidth_;
    int scanline_;
    int dot_;
    int masterClock_;
    int lineMasterClocks_;
    int totalScanlines_;
    u64 frameNumber_;
    bool pal_;
    bool field_;
    bool vblank_;
    bool frameReady_;
    bool hblankStartEvent_;
    bool hdmaStartEvent_;
    bool vblankStartEvent_;
    bool frameStartEvent_;
    bool refreshStartEvent_;
    bool rangeOver_;
    bool timeOver_;
    bool renderFrameEnabled_;
    bool frameSurfacePrepared_;
    bool frameHadHires_;
    unsigned renderedPixelsThisLine_;
    u64 blockedVramAccesses_;
    u64 blockedCgramAccesses_;
    u64 blockedOamAccesses_;
    // OBJ character fetches occur in HBlank of the preceding scanline.
    // Bit N records that the corresponding fetch slot was masked by INIDISP.
    // Keep separate completed/current and pending/next line masks.
    u64 objFetchBlockedCurrent_;
    u64 objFetchBlockedPending_;

    u8 displayControl_;
    u8 objectSelection_;
    u8 bgMode_;
    u8 mosaic_;
    u8 mainScreen_;
    u8 subScreen_;
    u8 setIni_;
    u8 vmain_;
    u16 vramAddress_;
    u16 vramReadBuffer_;
    u16 oamReloadAddress_;
    u16 oamAddress_;
    u8 oamWriteLatch_;
    bool oamWriteHigh_;
    bool oamPriorityRotation_;
    u8 oamFirstSprite_;
    u8 cgramAddress_;
    bool cgramHigh_;
    u8 cgramLatch_;
    u8 scrollLatch_;
    u8 horizontalScrollLatch_;
    u16 hCounterLatch_;
    u16 vCounterLatch_;
    bool hCounterHigh_;
    bool vCounterHigh_;
    u8 mode7Control_;
    s16 mode7A_;
    s16 mode7B_;
    s16 mode7C_;
    s16 mode7D_;
    s16 mode7X_;
    s16 mode7Y_;
    s16 mode7HOffset_;
    s16 mode7VOffset_;
    u8 mode7Latch_;
    bool mode7SecondWrite_;
    u8 ppu1OpenBus_;
    u8 ppu2OpenBus_;
    bool countersLatched_;
    bool counterLatchEnabled_;
    bool counterLatchPending_;
    u16 pendingCounterH_;
    u16 pendingCounterV_;
    u64 pendingCounterFrame_;

    u8 windowSelect_[3];
    u8 windowLeft_[2];
    u8 windowRight_[2];
    u8 windowLogic_[2];
    u8 mainWindowMask_;
    u8 subWindowMask_;
    u8 colorMathControl_;
    u8 colorMathEnable_;
    u16 fixedColor_;
};

} // namespace snes

#endif
