#include "snes/core/DSP.hpp"
#include <algorithm>
#include <cstring>

namespace snes {
namespace {

static int signExtend4(unsigned value) {
    return (value & 8u) ? static_cast<int>(value) - 16 : static_cast<int>(value);
}

static const unsigned RatePeriods[32] = {
    0, 2048, 1536, 1280, 1024, 768, 640, 512,
    384, 320, 256, 192, 160, 128, 96, 80,
    64, 48, 40, 32, 24, 20, 16, 12,
    10, 8, 6, 5, 4, 3, 2, 1
};

static const unsigned RateOffsets[32] = {
    0,0,1040,536,0,1040,536,0,1040,536,0,1040,536,0,1040,536,
    0,1040,536,0,1040,536,0,1040,536,0,1040,536,0,1040,536,0
};

// The S-DSP contains this measured 512-entry coefficient ROM. Keeping the
// original integer values also preserves its documented +/-1 sum error; a
// generated mathematical Gaussian cannot reproduce that hardware behaviour.
static const int Gaussian[512] = {
    0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000,
    0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x002,0x002,0x002,0x002,0x002,
    0x002,0x002,0x003,0x003,0x003,0x003,0x003,0x004,0x004,0x004,0x004,0x004,0x005,0x005,0x005,0x005,
    0x006,0x006,0x006,0x006,0x007,0x007,0x007,0x008,0x008,0x008,0x009,0x009,0x009,0x00A,0x00A,0x00A,
    0x00B,0x00B,0x00B,0x00C,0x00C,0x00D,0x00D,0x00E,0x00E,0x00F,0x00F,0x00F,0x010,0x010,0x011,0x011,
    0x012,0x013,0x013,0x014,0x014,0x015,0x015,0x016,0x017,0x017,0x018,0x018,0x019,0x01A,0x01B,0x01B,
    0x01C,0x01D,0x01D,0x01E,0x01F,0x020,0x020,0x021,0x022,0x023,0x024,0x024,0x025,0x026,0x027,0x028,
    0x029,0x02A,0x02B,0x02C,0x02D,0x02E,0x02F,0x030,0x031,0x032,0x033,0x034,0x035,0x036,0x037,0x038,
    0x03A,0x03B,0x03C,0x03D,0x03E,0x040,0x041,0x042,0x043,0x045,0x046,0x047,0x049,0x04A,0x04C,0x04D,
    0x04E,0x050,0x051,0x053,0x054,0x056,0x057,0x059,0x05A,0x05C,0x05E,0x05F,0x061,0x063,0x064,0x066,
    0x068,0x06A,0x06B,0x06D,0x06F,0x071,0x073,0x075,0x076,0x078,0x07A,0x07C,0x07E,0x080,0x082,0x084,
    0x086,0x089,0x08B,0x08D,0x08F,0x091,0x093,0x096,0x098,0x09A,0x09C,0x09F,0x0A1,0x0A3,0x0A6,0x0A8,
    0x0AB,0x0AD,0x0AF,0x0B2,0x0B4,0x0B7,0x0BA,0x0BC,0x0BF,0x0C1,0x0C4,0x0C7,0x0C9,0x0CC,0x0CF,0x0D2,
    0x0D4,0x0D7,0x0DA,0x0DD,0x0E0,0x0E3,0x0E6,0x0E9,0x0EC,0x0EF,0x0F2,0x0F5,0x0F8,0x0FB,0x0FE,0x101,
    0x104,0x107,0x10B,0x10E,0x111,0x114,0x118,0x11B,0x11E,0x122,0x125,0x129,0x12C,0x130,0x133,0x137,
    0x13A,0x13E,0x141,0x145,0x148,0x14C,0x150,0x153,0x157,0x15B,0x15F,0x162,0x166,0x16A,0x16E,0x172,
    0x176,0x17A,0x17D,0x181,0x185,0x189,0x18D,0x191,0x195,0x19A,0x19E,0x1A2,0x1A6,0x1AA,0x1AE,0x1B2,
    0x1B7,0x1BB,0x1BF,0x1C3,0x1C8,0x1CC,0x1D0,0x1D5,0x1D9,0x1DD,0x1E2,0x1E6,0x1EB,0x1EF,0x1F3,0x1F8,
    0x1FC,0x201,0x205,0x20A,0x20F,0x213,0x218,0x21C,0x221,0x226,0x22A,0x22F,0x233,0x238,0x23D,0x241,
    0x246,0x24B,0x250,0x254,0x259,0x25E,0x263,0x267,0x26C,0x271,0x276,0x27B,0x280,0x284,0x289,0x28E,
    0x293,0x298,0x29D,0x2A2,0x2A6,0x2AB,0x2B0,0x2B5,0x2BA,0x2BF,0x2C4,0x2C9,0x2CE,0x2D3,0x2D8,0x2DC,
    0x2E1,0x2E6,0x2EB,0x2F0,0x2F5,0x2FA,0x2FF,0x304,0x309,0x30E,0x313,0x318,0x31D,0x322,0x326,0x32B,
    0x330,0x335,0x33A,0x33F,0x344,0x349,0x34E,0x353,0x357,0x35C,0x361,0x366,0x36B,0x370,0x374,0x379,
    0x37E,0x383,0x388,0x38C,0x391,0x396,0x39B,0x39F,0x3A4,0x3A9,0x3AD,0x3B2,0x3B7,0x3BB,0x3C0,0x3C5,
    0x3C9,0x3CE,0x3D2,0x3D7,0x3DC,0x3E0,0x3E5,0x3E9,0x3ED,0x3F2,0x3F6,0x3FB,0x3FF,0x403,0x408,0x40C,
    0x410,0x415,0x419,0x41D,0x421,0x425,0x42A,0x42E,0x432,0x436,0x43A,0x43E,0x442,0x446,0x44A,0x44E,
    0x452,0x455,0x459,0x45D,0x461,0x465,0x468,0x46C,0x470,0x473,0x477,0x47A,0x47E,0x481,0x485,0x488,
    0x48C,0x48F,0x492,0x496,0x499,0x49C,0x49F,0x4A2,0x4A6,0x4A9,0x4AC,0x4AF,0x4B2,0x4B5,0x4B7,0x4BA,
    0x4BD,0x4C0,0x4C3,0x4C5,0x4C8,0x4CB,0x4CD,0x4D0,0x4D2,0x4D5,0x4D7,0x4D9,0x4DC,0x4DE,0x4E0,0x4E3,
    0x4E5,0x4E7,0x4E9,0x4EB,0x4ED,0x4EF,0x4F1,0x4F3,0x4F5,0x4F6,0x4F8,0x4FA,0x4FB,0x4FD,0x4FF,0x500,
    0x502,0x503,0x504,0x506,0x507,0x508,0x50A,0x50B,0x50C,0x50D,0x50E,0x50F,0x510,0x511,0x511,0x512,
    0x513,0x514,0x514,0x515,0x516,0x516,0x517,0x517,0x517,0x518,0x518,0x518,0x518,0x518,0x519,0x519
};

} // namespace

DSP::Voice::Voice()
    : pitch(0), sourceNumber(0), leftVolume(0), rightVolume(0), keyedOn(false),
      phase(0), brrAddress(0), loopAddress(0), brrNibble(0), brrHeader(0),
      history1(0), history2(0), decoded(), envelope(0), envelopeState(0),
      envelopeCounter(0), ended(false) {
    std::memset(decoded, 0, sizeof(decoded));
}

DSP::DSP()
    : voices_(), registers_(), ram_(0), pendingSamples_(), noiseLfsr_(0x4000u),
      noiseCounter_(0), rateCounter_(0), echoHistoryLeft_(), echoHistoryRight_(),
      echoHistoryIndex_(0), echoOffset_(0), echoBaseLatched_(0), echoLengthLatched_(4), lastOutputLeft_(0), lastOutputRight_(0),
      recentPeak_(0), samplesGenerated_(0), nonSilentSamples_(0), voiceOutputMask_(0xFFu) {
    reset();
}

void DSP::connectRam(u8* ram) { ram_ = ram; }

void DSP::reset() {
    for (unsigned i = 0; i < VoiceCount; ++i) voices_[i] = Voice();
    std::memset(registers_, 0, sizeof(registers_));
    // The physical S-DSP powers up/reset with its internal FLG state at $E0:
    // soft reset, mute, and—critically—echo writes disabled.  Leaving FLG at
    // zero makes an unconfigured echo unit write through ESA=$00/EDL=$00 and
    // repeatedly destroy ARAM $0000-$0003 while the IPL is using those bytes
    // as its upload destination pointer.
    registers_[0x6Cu] = 0xE0u;
    std::memset(echoHistoryLeft_, 0, sizeof(echoHistoryLeft_));
    std::memset(echoHistoryRight_, 0, sizeof(echoHistoryRight_));
    pendingSamples_.clear();
    noiseLfsr_ = 0x4000u;
    noiseCounter_ = 0;
    rateCounter_ = 0;
    echoHistoryIndex_ = 0;
    echoOffset_ = 0;
    echoBaseLatched_ = 0;
    echoLengthLatched_ = 4;
    lastOutputLeft_ = 0;
    lastOutputRight_ = 0;
    recentPeak_ = 0;
    samplesGenerated_ = 0;
    nonSilentSamples_ = 0;
}

s16 DSP::clamp16(int value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<s16>(value);
}

u8 DSP::readRegister(u8 address) const {
    address &= 0x7Fu;
    const unsigned voice = address >> 4;
    const unsigned reg = address & 0x0Fu;
    if (voice < VoiceCount && reg == 8u)
        return static_cast<u8>((voices_[voice].envelope >> 4) & 0x7Fu);
    return registers_[address];
}

void DSP::writeRegister(u8 address, u8 value) {
    address &= 0x7Fu;
    registers_[address] = value;
    // Before the first sample, DSP initialization has not yet begun the
    // echo pipeline; initialize the address latch from an early ESA write.
    if (address == 0x6Du && samplesGenerated_ == 0u)
        echoBaseLatched_ = static_cast<u16>(value << 8);
    const unsigned voiceIndex = address >> 4;
    const unsigned reg = address & 0x0Fu;
    if (voiceIndex < VoiceCount) {
        Voice& voice = voices_[voiceIndex];
        if (reg == 0u) voice.leftVolume = static_cast<s8>(value);
        else if (reg == 1u) voice.rightVolume = static_cast<s8>(value);
        else if (reg == 2u) voice.pitch = static_cast<u16>((voice.pitch & 0x3F00u) | value);
        else if (reg == 3u) voice.pitch = static_cast<u16>((voice.pitch & 0x00FFu) | ((value & 0x3Fu) << 8));
        else if (reg == 4u) voice.sourceNumber = value;
    }

    if (address == 0x4Cu) {
        for (unsigned i = 0; i < VoiceCount; ++i)
            if (value & (1u << i)) keyOn(i);
        registers_[0x4Cu] = 0;
    } else if (address == 0x5Cu) {
        for (unsigned i = 0; i < VoiceCount; ++i)
            if (value & (1u << i)) keyOff(i);
    } else if (address == 0x7Cu) {
        // ENDX is cleared by any write on the physical DSP.
        registers_[0x7Cu] = 0;
    } else if (address == 0x6Cu) {
        if (value & 0x80u) {
            for (unsigned i = 0; i < VoiceCount; ++i) keyOff(i);
            noiseLfsr_ = 0x4000u;
        }
    }
    // ESA and EDL are live control registers. Writing either must not
    // restart echo address progression or wipe the 8-tap FIR history;
    // the echo pointer wraps naturally on the configured delay boundary.
}

void DSP::keyOn(unsigned index) {
    Voice& voice = voices_[index];
    voice.keyedOn = true;
    voice.ended = false;
    voice.phase = 0;
    voice.brrNibble = 0;
    voice.history1 = 0;
    voice.history2 = 0;
    voice.envelope = 0;
    voice.envelopeState = 0;
    voice.envelopeCounter = 0;
    registers_[0x7Cu] &= static_cast<u8>(~(1u << index));

    if (!ram_) { voice.keyedOn = false; return; }
    const u16 directory = static_cast<u16>(registers_[0x5Du] << 8);
    const u16 entry = static_cast<u16>(directory + voice.sourceNumber * 4u);
    voice.brrAddress = static_cast<u16>(ram_[entry] | (static_cast<u16>(ram_[static_cast<u16>(entry + 1u)]) << 8));
    voice.loopAddress = static_cast<u16>(ram_[static_cast<u16>(entry + 2u)] |
                                        (static_cast<u16>(ram_[static_cast<u16>(entry + 3u)]) << 8));
    decodeBlock(voice);
}

void DSP::keyOff(unsigned index) {
    Voice& voice = voices_[index];
    if (voice.keyedOn || voice.envelope > 0) voice.envelopeState = 3;
}

void DSP::decodeBlock(Voice& voice) {
    if (!ram_) { std::memset(voice.decoded, 0, sizeof(voice.decoded)); return; }
    const u8 header = ram_[voice.brrAddress];
    voice.brrHeader = header;
    const int range = header >> 4;
    const int filter = (header >> 2) & 3;

    for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
        const u8 packed = ram_[static_cast<u16>(voice.brrAddress + 1u + sampleIndex / 2)];
        const unsigned nibble = (sampleIndex & 1) ? (packed & 0x0Fu) : (packed >> 4);
        int sample = signExtend4(nibble);
        if (range <= 12) {
            // Avoid left-shifting a negative signed value (undefined in C++).
            // BRR range zero requires an arithmetic divide by two; the other
            // legal ranges are exact powers-of-two multiplications.
            if (range == 0)
                sample = sample >= 0 ? sample / 2 : -(((-sample) + 1) / 2);
            else
                sample *= (1 << (range - 1));
        } else {
            sample = sample < 0 ? -2048 : 0;
        }

        const int h1 = voice.history1;
        const int h2 = voice.history2;
        if (filter == 1) sample += h1 + ((-h1) >> 4);
        else if (filter == 2) sample += (h1 * 2) + ((-3 * h1) >> 5) - h2 + (h2 >> 4);
        else if (filter == 3) sample += (h1 * 2) + ((-13 * h1) >> 6) - h2 + ((3 * h2) >> 4);

        sample = std::max(-32768, std::min(32767, sample));
        sample &= ~1;
        voice.history2 = voice.history1;
        voice.history1 = static_cast<s16>(sample);
        voice.decoded[sampleIndex] = static_cast<s16>(sample);
    }
}

void DSP::updateEnvelope(unsigned index) {
    Voice& voice = voices_[index];
    if (!voice.keyedOn && voice.envelopeState != 3) return;

    if (voice.envelopeState == 3) {
        voice.envelope -= 8;
        if (voice.envelope <= 0) {
            voice.envelope = 0;
            voice.keyedOn = false;
        }
        return;
    }

    const u8 adsr1 = registers_[index * 16u + 5u];
    const u8 adsr2 = registers_[index * 16u + 6u];
    const u8 gain = registers_[index * 16u + 7u];

    if ((adsr1 & 0x80u) == 0) {
        if ((gain & 0x80u) == 0) {
            voice.envelope = (gain & 0x7Fu) << 4;
            return;
        }
        const unsigned rate = gain & 0x1Fu;
        if (!rateTriggered(rate)) return;
        ++voice.envelopeCounter;
        switch ((gain >> 5) & 3u) {
            case 0: voice.envelope -= 32; break;
            case 1: voice.envelope -= 1; voice.envelope -= voice.envelope >> 8; break;
            case 2: voice.envelope += 32; break;
            default: voice.envelope += voice.envelope < 0x600 ? 32 : 8; break;
        }
        voice.envelope = std::max(0, std::min(0x7FF, voice.envelope));
        return;
    }

    if (voice.envelopeState == 0) {
        const unsigned attack = adsr1 & 0x0Fu;
        const unsigned rate = attack == 15u ? 31u : attack * 2u + 1u;
        if (rateTriggered(rate)) {
            ++voice.envelopeCounter;
            voice.envelope += attack == 15u ? 1024 : 32;
            if (voice.envelope >= 0x7FF) {
                voice.envelope = 0x7FF;
                voice.envelopeState = 1;
                voice.envelopeCounter = 0;
            }
        }
    } else if (voice.envelopeState == 1) {
        const unsigned rate = ((adsr1 >> 4) & 7u) * 2u + 16u;
        if (rateTriggered(rate)) {
            ++voice.envelopeCounter;
            voice.envelope -= 1;
            voice.envelope -= voice.envelope >> 8;
        }
        const int sustainLevel = (((adsr2 >> 5) & 7u) + 1u) * 0x100;
        if (voice.envelope <= sustainLevel) {
            voice.envelopeState = 2;
            voice.envelopeCounter = 0;
        }
    } else {
        const unsigned rate = adsr2 & 0x1Fu;
        if (rateTriggered(rate)) {
            ++voice.envelopeCounter;
            voice.envelope -= 1;
            voice.envelope -= voice.envelope >> 8;
        }
    }
    voice.envelope = std::max(0, std::min(0x7FF, voice.envelope));
}

s16 DSP::interpolatedSample(const Voice& voice) const {
    const int index = voice.brrNibble & 15;
    const int positions[4] = {
        std::max(0, index - 1), index, std::min(15, index + 1), std::min(15, index + 2)
    };
    const unsigned fraction = (voice.phase >> 4) & 0xFFu;
    int sample = (Gaussian[0x0FFu - fraction] * static_cast<int>(voice.decoded[positions[0]])) >> 10;
    sample += (Gaussian[0x1FFu - fraction] * static_cast<int>(voice.decoded[positions[1]])) >> 10;
    sample += (Gaussian[0x100u + fraction] * static_cast<int>(voice.decoded[positions[2]])) >> 10;
    // The hardware truncates the three-tap partial sum to its signed 16-bit
    // accumulator before adding and saturating the final tap.
    sample = static_cast<s16>(sample);
    sample += (Gaussian[fraction] * static_cast<int>(voice.decoded[positions[3]])) >> 10;
    return static_cast<s16>(clamp16(sample) >> 1);
}

bool DSP::rateTriggered(unsigned rate) const {
    rate &= 31u;
    const unsigned period = RatePeriods[rate];
    return period != 0u && ((rateCounter_ + RateOffsets[rate]) % period) == 0u;
}

s16 DSP::nextVoiceSample(unsigned index, unsigned effectivePitch) {
    Voice& voice = voices_[index];
    if (!voice.keyedOn || voice.ended) return 0;

    updateEnvelope(index);
    if (!voice.keyedOn) return 0;

    s16 sample = interpolatedSample(voice);
    if (registers_[0x3Du] & (1u << index))
        sample = static_cast<s16>((static_cast<s16>(noiseLfsr_ << 1)) >> 1);
    sample = static_cast<s16>((static_cast<int>(sample) * voice.envelope) >> 11);

    voice.phase += std::min(0x3FFFu, effectivePitch);
    while (voice.phase >= 0x1000u) {
        voice.phase -= 0x1000u;
        ++voice.brrNibble;
        if (voice.brrNibble >= 16) {
            const bool end = (voice.brrHeader & 1) != 0;
            const bool loop = (voice.brrHeader & 2) != 0;
            if (end) {
                registers_[0x7Cu] |= static_cast<u8>(1u << index);
                if (loop) voice.brrAddress = voice.loopAddress;
                else { voice.ended = true; voice.keyedOn = false; return 0; }
            } else voice.brrAddress = static_cast<u16>(voice.brrAddress + 9u);
            voice.brrNibble = 0;
            decodeBlock(voice);
        }
    }
    registers_[index * 16u + 8u] = static_cast<u8>((voice.envelope >> 4) & 0x7Fu);
    registers_[index * 16u + 9u] = static_cast<u8>(sample >> 8);
    return sample;
}

s16 DSP::readEchoSample(u16 address) const {
    if (!ram_) return 0;
    const u8 low = ram_[address];
    const u8 high = ram_[static_cast<u16>(address + 1u)];
    return static_cast<s16>(static_cast<u16>(low | (static_cast<u16>(high) << 8)));
}

void DSP::writeEchoSample(u16 address, s16 value) {
    if (!ram_) return;
    ram_[address] = static_cast<u8>(value);
    ram_[static_cast<u16>(address + 1u)] = static_cast<u8>(static_cast<u16>(value) >> 8);
}

void DSP::stepSample() {
    rateCounter_ = rateCounter_ == 0u ? 0x77FFu : rateCounter_ - 1u;
    const unsigned noiseRate = registers_[0x6Cu] & 0x1Fu;
    if (rateTriggered(noiseRate)) {
        ++noiseCounter_;
        const u16 feedback = static_cast<u16>((noiseLfsr_ ^ (noiseLfsr_ >> 1)) & 1u);
        noiseLfsr_ = static_cast<u16>((noiseLfsr_ >> 1) | (feedback << 14));
    }

    int dryLeft = 0;
    int dryRight = 0;
    int echoInputLeft = 0;
    int echoInputRight = 0;
    s16 previous = 0;
    for (unsigned i = 0; i < VoiceCount; ++i) {
        unsigned effectivePitch = voices_[i].pitch;
        if (i > 0 && (registers_[0x2Du] & (1u << i))) {
            const int modulation = (static_cast<int>(previous) * static_cast<int>(effectivePitch)) >> 15;
            effectivePitch = static_cast<unsigned>(std::max(0, std::min(0x3FFF,
                static_cast<int>(effectivePitch) + modulation)));
        }
        const s16 sample = nextVoiceSample(i, effectivePitch);
        previous = sample;
        const int voiceLeft = (static_cast<int>(sample) * voices_[i].leftVolume) >> 7;
        const int voiceRight = (static_cast<int>(sample) * voices_[i].rightVolume) >> 7;
        if (voiceOutputMask_ & (1u << i)) {
            dryLeft += voiceLeft;
            dryRight += voiceRight;
            if (registers_[0x4Du] & (1u << i)) {
                echoInputLeft += voiceLeft;
                echoInputRight += voiceRight;
            }
        }
    }

    // ESA is captured at the end of the preceding DSP sample period,
    // not sampled from the live CPU-visible register at the echo read.
    // Changing ESA while echo is active therefore has a one-sample pipeline.
    const u16 echoAddress = static_cast<u16>(echoBaseLatched_ + echoOffset_);
    // The DSP's echo FIR operates on signed 15-bit history entries (the
    // samples fetched from ARAM are first divided by two), and each tap
    // individually truncates its 1.7 fixed-point product by six bits.
    // Intermediate taps 0..6 wrap to 16 bits, *only* the final seventh-index
    // tap uses saturation. Saturating the whole 32-bit sum, as CP20 did,
    // produces different results whenever the FIR gain exceeds unity.
    echoHistoryLeft_[echoHistoryIndex_] = static_cast<s16>(readEchoSample(echoAddress) >> 1);
    echoHistoryRight_[echoHistoryIndex_] = static_cast<s16>(
        readEchoSample(static_cast<u16>(echoAddress + 2u)) >> 1);

    auto fir = [&](const s16* history) -> int {
        int sum = 0;
        for (unsigned tap = 0; tap < 7u; ++tap) {
            const unsigned sampleIndex = (echoHistoryIndex_ - tap) & 7u;
            const int coefficient = static_cast<s8>(registers_[tap * 0x10u + 0x0Fu]);
            sum += (static_cast<int>(history[sampleIndex]) * coefficient) >> 6;
        }
        // Wrap (not clamp) the seven-tap partial sum to signed 16 bits.
        const unsigned wrapped = static_cast<unsigned>(sum) & 0xFFFFu;
        sum = wrapped < 0x8000u ? static_cast<int>(wrapped) : static_cast<int>(wrapped) - 0x10000;
        const int lastCoeff = static_cast<s8>(registers_[0x7Fu]);
        const unsigned lastIndex = (echoHistoryIndex_ - 7u) & 7u;
        sum += (static_cast<int>(history[lastIndex]) * lastCoeff) >> 6;
        return static_cast<int>(clamp16(sum)) & ~1;
    };
    const int filteredLeft = fir(echoHistoryLeft_);
    const int filteredRight = fir(echoHistoryRight_);

    int left = (dryLeft * static_cast<s8>(registers_[0x0Cu])) >> 7;
    int right = (dryRight * static_cast<s8>(registers_[0x1Cu])) >> 7;
    left += (filteredLeft * static_cast<s8>(registers_[0x2Cu])) >> 7;
    right += (filteredRight * static_cast<s8>(registers_[0x3Cu])) >> 7;

    const int feedback = static_cast<s8>(registers_[0x0Du]);
    const s16 echoWriteLeft = clamp16(echoInputLeft + ((filteredLeft * feedback) >> 7));
    const s16 echoWriteRight = clamp16(echoInputRight + ((filteredRight * feedback) >> 7));
    if ((registers_[0x6Cu] & 0x20u) == 0) {
        writeEchoSample(echoAddress, echoWriteLeft);
        writeEchoSample(static_cast<u16>(echoAddress + 2u), echoWriteRight);
    }

    echoHistoryIndex_ = (echoHistoryIndex_ + 1u) & 7u;
    // EDL is only sampled when starting a new echo buffer traversal.
    // A mid-buffer EDL write must not truncate the current traversal.
    echoBaseLatched_ = static_cast<u16>(registers_[0x6Du] << 8);
    if (echoOffset_ == 0u) {
        echoLengthLatched_ = static_cast<unsigned>(registers_[0x7Du] & 0x0Fu) * 0x800u;
        if (!echoLengthLatched_) echoLengthLatched_ = 4u;
    }
    echoOffset_ += 4u;
    if (echoOffset_ >= echoLengthLatched_) echoOffset_ = 0;

    if (registers_[0x6Cu] & 0x40u) left = right = 0;
    lastOutputLeft_ = clamp16(left);
    lastOutputRight_ = clamp16(right);
    const unsigned magnitudeLeft = lastOutputLeft_ < 0 ? static_cast<unsigned>(-static_cast<int>(lastOutputLeft_)) : static_cast<unsigned>(lastOutputLeft_);
    const unsigned magnitudeRight = lastOutputRight_ < 0 ? static_cast<unsigned>(-static_cast<int>(lastOutputRight_)) : static_cast<unsigned>(lastOutputRight_);
    const u16 magnitude = static_cast<u16>(std::min(32767u, std::max(magnitudeLeft, magnitudeRight)));
    // Slowly decay the diagnostic peak so the frontend can distinguish a
    // genuinely silent DSP from a host-device problem without scanning PCM.
    if (recentPeak_ > 0u) --recentPeak_;
    if (magnitude > recentPeak_) recentPeak_ = magnitude;
    ++samplesGenerated_;
    if (magnitude != 0u) ++nonSilentSamples_;
    pendingSamples_.push_back(lastOutputLeft_);
    pendingSamples_.push_back(lastOutputRight_);
}

const DSP::Voice& DSP::voice(unsigned index) const { return voices_[index < VoiceCount ? index : 0]; }
const std::vector<s16>& DSP::pendingSamples() const { return pendingSamples_; }
void DSP::clearPendingSamples() { pendingSamples_.clear(); }
s16 DSP::lastOutputLeft() const { return lastOutputLeft_; }
s16 DSP::lastOutputRight() const { return lastOutputRight_; }
u16 DSP::recentPeak() const { return recentPeak_; }
u64 DSP::samplesGenerated() const { return samplesGenerated_; }
u64 DSP::nonSilentSamples() const { return nonSilentSamples_; }
void DSP::setVoiceOutputMask(u8 mask) { voiceOutputMask_ = mask; }
u8 DSP::voiceOutputMask() const { return voiceOutputMask_; }

unsigned DSP::activeVoiceCount() const {
    unsigned count = 0;
    for (unsigned i = 0; i < VoiceCount; ++i)
        if (voices_[i].keyedOn || voices_[i].envelope > 0) ++count;
    return count;
}

} // namespace snes
