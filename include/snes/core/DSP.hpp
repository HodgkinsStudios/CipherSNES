#ifndef SNES_CORE_DSP_HPP
#define SNES_CORE_DSP_HPP

#include <vector>
#include "snes/Types.hpp"
#include "snes/core/Component.hpp"

namespace snes {

class DSP : public Component {
    friend class SaveStateManager;
public:
    enum { VoiceCount = 8, RegisterCount = 128 };

    struct Voice {
        Voice();
        u16 pitch;
        u8 sourceNumber;
        s8 leftVolume;
        s8 rightVolume;
        bool keyedOn;
        u32 phase;
        u16 brrAddress;
        u16 loopAddress;
        int brrNibble;
        int brrHeader;
        s16 history1;
        s16 history2;
        s16 decoded[16];
        int envelope;
        int envelopeState;
        int envelopeCounter;
        bool ended;
    };

    DSP();
    void connectRam(u8* ram);
    void reset();
    void stepSample();
    u8 readRegister(u8 address) const;
    void writeRegister(u8 address, u8 value);
    const Voice& voice(unsigned index) const;
    const std::vector<s16>& pendingSamples() const;
    void clearPendingSamples();
    s16 lastOutputLeft() const;
    s16 lastOutputRight() const;
    u16 recentPeak() const;
    u64 samplesGenerated() const;
    u64 nonSilentSamples() const;
    unsigned activeVoiceCount() const;
    void setVoiceOutputMask(u8 mask);
    u8 voiceOutputMask() const;

private:
    void keyOn(unsigned index);
    void keyOff(unsigned index);
    void decodeBlock(Voice& voice);
    s16 nextVoiceSample(unsigned index, unsigned effectivePitch);
    s16 interpolatedSample(const Voice& voice) const;
    void updateEnvelope(unsigned index);
    bool rateTriggered(unsigned rate) const;
    s16 readEchoSample(u16 address) const;
    void writeEchoSample(u16 address, s16 value);
    static s16 clamp16(int value);

    Voice voices_[VoiceCount];
    u8 registers_[RegisterCount];
    u8* ram_;
    std::vector<s16> pendingSamples_;
    u16 noiseLfsr_;
    unsigned noiseCounter_;
    unsigned rateCounter_;
    s16 echoHistoryLeft_[8];
    s16 echoHistoryRight_[8];
    unsigned echoHistoryIndex_;
    unsigned echoOffset_;
    u16 echoBaseLatched_;
    unsigned echoLengthLatched_;
    s16 lastOutputLeft_;
    s16 lastOutputRight_;
    u16 recentPeak_;
    u64 samplesGenerated_;
    u64 nonSilentSamples_;
    u8 voiceOutputMask_;
};

} // namespace snes

#endif
