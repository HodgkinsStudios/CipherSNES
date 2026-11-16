#include "snes/core/Controller.hpp"

namespace snes {

Controller::Controller()
    : state_(0), shift_(0), serialPosition_(0), latched_(false), device_(Joypad),
      mouseSensitivity_(0), mouseLeft_(false), mouseRight_(false),
      mouseXNegative_(false), mouseYNegative_(false), mouseDeltaX_(0), mouseDeltaY_(0),
      lightGunButtons_(), scopeTurboSwitch_(false), scopeTurboReported_(false),
      scopeFirePulse_(false), scopePausePulse_(false), scopeFireSerial_(false),
      scopePauseSerial_(false), lightGunX_(), lightGunY_(), lightGunOffscreen_(),
      justifierLatchedGun_(0), justifierNextGun_(0) {}

void Controller::reset() {
    state_ = 0;
    shift_ = 0;
    serialPosition_ = 0;
    latched_ = false;
    mouseSensitivity_ = 0;
    mouseLeft_ = false;
    mouseRight_ = false;
    mouseXNegative_ = false;
    mouseYNegative_ = false;
    mouseDeltaX_ = 0;
    mouseDeltaY_ = 0;
    for (unsigned i = 0; i < 5u; ++i) lightGunButtons_[i] = false;
    scopeTurboSwitch_ = false;
    scopeTurboReported_ = false;
    scopeFirePulse_ = false;
    scopePausePulse_ = false;
    scopeFireSerial_ = false;
    scopePauseSerial_ = false;
    for (unsigned i = 0; i < 2u; ++i) {
        lightGunX_[i] = 128;
        lightGunY_[i] = 112;
        lightGunOffscreen_[i] = true;
    }
    justifierLatchedGun_ = 0;
    justifierNextGun_ = 0;
}

void Controller::setDevice(Device device) {
    device_ = device;
    serialPosition_ = 0;
    shift_ = serialPacket();
}

Controller::Device Controller::device() const { return device_; }

void Controller::setButton(Button button, bool pressed) {
    const u16 mask = static_cast<u16>(1u << static_cast<unsigned>(button));
    if (pressed) state_ |= mask;
    else state_ &= static_cast<u16>(~mask);
    if (latched_) shift_ = serialPacket();
}

bool Controller::button(Button button) const {
    return (state_ & static_cast<u16>(1u << static_cast<unsigned>(button))) != 0;
}

void Controller::setMouseButton(MouseButton button, bool pressed) {
    if (button == MouseLeft) mouseLeft_ = pressed;
    else if (button == MouseRight) mouseRight_ = pressed;
    if (latched_) shift_ = serialPacket();
}

bool Controller::mouseButton(MouseButton button) const {
    return button == MouseLeft ? mouseLeft_ : mouseRight_;
}

void Controller::moveMouse(int deltaX, int deltaY) {
    if (deltaX != 0) {
        mouseXNegative_ = deltaX < 0;
        const unsigned magnitude = static_cast<unsigned>(deltaX < 0 ? -static_cast<long long>(deltaX) : deltaX);
        const unsigned total = static_cast<unsigned>(mouseDeltaX_) + magnitude;
        mouseDeltaX_ = static_cast<u8>(total > 127u ? 127u : total);
    }
    if (deltaY != 0) {
        mouseYNegative_ = deltaY < 0;
        const unsigned magnitude = static_cast<unsigned>(deltaY < 0 ? -static_cast<long long>(deltaY) : deltaY);
        const unsigned total = static_cast<unsigned>(mouseDeltaY_) + magnitude;
        mouseDeltaY_ = static_cast<u8>(total > 127u ? 127u : total);
    }
    if (latched_) shift_ = serialPacket();
}

u8 Controller::mouseSensitivity() const { return mouseSensitivity_; }

void Controller::setLightGunButton(LightGunButton button, bool pressed) {
    const unsigned index = static_cast<unsigned>(button);
    if (index >= 5u) return;
    const bool rising = pressed && !lightGunButtons_[index];
    lightGunButtons_[index] = pressed;
    if (button == GunTrigger1 && rising) {
        scopeFirePulse_ = true;
        scopeTurboReported_ = scopeTurboSwitch_;
    }
    if (button == GunPauseOrStart1 && rising) scopePausePulse_ = true;
    if (latched_) shift_ = serialPacket();
}

bool Controller::lightGunButton(LightGunButton button) const {
    const unsigned index = static_cast<unsigned>(button);
    return index < 5u && lightGunButtons_[index];
}

void Controller::setLightGunAim(unsigned gun, int x, int y, bool offscreen) {
    if (gun >= 2u) return;
    lightGunX_[gun] = static_cast<s16>(x < -32768 ? -32768 : (x > 32767 ? 32767 : x));
    lightGunY_[gun] = static_cast<s16>(y < -32768 ? -32768 : (y > 32767 ? 32767 : y));
    lightGunOffscreen_[gun] = offscreen || x < 0 || y < 0 || x >= 256 || y >= 240;
    if (latched_) shift_ = serialPacket();
}

void Controller::setSuperScopeTurbo(bool enabled) {
    if (enabled && !scopeTurboSwitch_ && lightGunButtons_[GunTrigger1]) {
        scopeFirePulse_ = true;
        scopeTurboReported_ = true;
    }
    scopeTurboSwitch_ = enabled;
    if (latched_) shift_ = serialPacket();
}

bool Controller::superScopeTurbo() const { return scopeTurboSwitch_; }

bool Controller::lightGunLatchTarget(int& horizontal, int& vertical) const {
    if (device_ == SuperScope) {
        if ((!scopeFireSerial_ && !lightGunButtons_[GunAuxiliary1]) || lightGunOffscreen_[0]) return false;
        horizontal = static_cast<int>(lightGunX_[0]) + 40;
        vertical = static_cast<int>(lightGunY_[0]) + 1;
        return true;
    }
    if (device_ == Justifier) {
        const unsigned gun = justifierLatchedGun_ & 1u;
        if (lightGunOffscreen_[gun]) return false;
        horizontal = static_cast<int>(lightGunX_[gun]) + 40;
        vertical = static_cast<int>(lightGunY_[gun]) + 1;
        return true;
    }
    return false;
}

u16 Controller::state() const { return state_; }
u16 Controller::autoJoypadWord() const {
    const u32 packet = shift_;
    u16 result = 0;
    for (unsigned i = 0; i < 16u; ++i) {
        if (packet & (static_cast<u32>(1u) << i))
            result |= static_cast<u16>(1u << (15u - i));
    }
    return result;
}

void Controller::latch(bool enabled) {
    const bool wasLatched = latched_;
    latched_ = enabled;
    if (enabled) {
        serialPosition_ = 0;
        captureSerialPacket();
    } else if (wasLatched) {
        serialPosition_ = 0;
        shift_ = serialPacket();
    }
}

u8 Controller::readSerial() {
    if (device_ == Disconnected) return 0;
    if (latched_) {
        if (device_ == Mouse) {
            mouseSensitivity_ = static_cast<u8>((mouseSensitivity_ + 1u) % 3u);
            shift_ = serialPacket();
        } else shift_ = serialPacket();
        return static_cast<u8>(shift_ & 1u);
    }

    const unsigned packetBits = serialPacketBits();
    const u8 value = serialPosition_ < packetBits
        ? static_cast<u8>((shift_ >> serialPosition_) & 1u)
        : 1u;
    if (serialPosition_ < packetBits) ++serialPosition_;
    if (device_ == Mouse && serialPosition_ == 32u) clearMouseMotion();
    return value;
}

u32 Controller::serialPacket() const {
    if (device_ == Disconnected) return 0;
    if (device_ == Joypad) return state_;

    if (device_ == SuperScope) {
        u32 packet = 0x0000FF00u;
        if (scopeFireSerial_) packet |= 1u << 0;
        if (lightGunButtons_[GunAuxiliary1]) packet |= 1u << 1;
        if (scopeTurboReported_) packet |= 1u << 2;
        if (scopePauseSerial_) packet |= 1u << 3;
        if ((scopeFireSerial_ || lightGunButtons_[GunAuxiliary1]) && lightGunOffscreen_[0]) packet |= 1u << 6;
        return packet;
    }

    if (device_ == Justifier) {
        u32 packet = 0x00AA7000u;
        if (lightGunButtons_[GunTrigger1]) packet |= 1u << 24;
        if (lightGunButtons_[GunTrigger2]) packet |= 1u << 25;
        if (lightGunButtons_[GunPauseOrStart1]) packet |= 1u << 26;
        if (lightGunButtons_[GunStart2]) packet |= 1u << 27;
        if ((justifierLatchedGun_ & 1u) == 0u) packet |= 1u << 28;
        return packet;
    }

    // SNES Mouse serial packet, first bit read in bit 0. Direction and
    // magnitude fields are transmitted MSB-first, while the packet itself is
    // shifted LSB-first by the controller port.
    u32 packet = 0;
    if (mouseRight_) packet |= static_cast<u32>(1u) << 8;
    if (mouseLeft_) packet |= static_cast<u32>(1u) << 9;
    packet |= static_cast<u32>((mouseSensitivity_ >> 1) & 1u) << 10;
    packet |= static_cast<u32>(mouseSensitivity_ & 1u) << 11;
    packet |= static_cast<u32>(1u) << 15;
    if (mouseYNegative_) packet |= static_cast<u32>(1u) << 16;
    if (mouseXNegative_) packet |= static_cast<u32>(1u) << 24;
    for (unsigned i = 0; i < 7u; ++i) {
        if (mouseDeltaY_ & static_cast<u8>(1u << (6u - i))) packet |= static_cast<u32>(1u) << (17u + i);
        if (mouseDeltaX_ & static_cast<u8>(1u << (6u - i))) packet |= static_cast<u32>(1u) << (25u + i);
    }
    return packet;
}

unsigned Controller::serialPacketBits() const {
    return device_ == Mouse || device_ == Justifier ? 32u : 16u;
}

void Controller::captureSerialPacket() {
    if (device_ == SuperScope) {
        scopeFireSerial_ = scopeTurboSwitch_ ? lightGunButtons_[GunTrigger1] : scopeFirePulse_;
        scopePauseSerial_ = scopePausePulse_;
        scopeFirePulse_ = false;
        scopePausePulse_ = false;
    } else if (device_ == Justifier) {
        justifierLatchedGun_ = justifierNextGun_;
        justifierNextGun_ ^= 1u;
    }
    shift_ = serialPacket();
}

void Controller::clearMouseMotion() {
    mouseDeltaX_ = 0;
    mouseDeltaY_ = 0;
}

} // namespace snes
