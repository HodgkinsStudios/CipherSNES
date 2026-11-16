#ifndef SNES_CORE_CONTROLLER_HPP
#define SNES_CORE_CONTROLLER_HPP

#include "snes/Types.hpp"
#include "snes/core/Component.hpp"

namespace snes {

class Controller : public Component {
    friend class SaveStateManager;
public:
    enum Button {
        B = 0, Y, Select, Start, Up, Down, Left, Right,
        A, X, L, R
    };
    enum Device {
        Joypad = 0,
        Mouse,
        SuperScope,
        Justifier,
        Disconnected
    };
    enum MouseButton {
        MouseLeft = 0,
        MouseRight
    };
    enum LightGunButton {
        GunTrigger1 = 0,
        GunAuxiliary1,
        GunPauseOrStart1,
        GunTrigger2,
        GunStart2
    };

    Controller();
    void reset();
    void setDevice(Device device);
    Device device() const;
    void setButton(Button button, bool pressed);
    bool button(Button button) const;
    void setMouseButton(MouseButton button, bool pressed);
    bool mouseButton(MouseButton button) const;
    void moveMouse(int deltaX, int deltaY);
    u8 mouseSensitivity() const;
    void setLightGunButton(LightGunButton button, bool pressed);
    bool lightGunButton(LightGunButton button) const;
    void setLightGunAim(unsigned gun, int x, int y, bool offscreen = false);
    void setSuperScopeTurbo(bool enabled);
    bool superScopeTurbo() const;
    bool lightGunLatchTarget(int& horizontal, int& vertical) const;
    u16 state() const;
    u16 autoJoypadWord() const;
    void latch(bool enabled);
    u8 readSerial();

private:
    u32 serialPacket() const;
    unsigned serialPacketBits() const;
    void captureSerialPacket();
    void clearMouseMotion();

    u16 state_;
    u32 shift_;
    unsigned serialPosition_;
    bool latched_;
    Device device_;
    u8 mouseSensitivity_;
    bool mouseLeft_;
    bool mouseRight_;
    bool mouseXNegative_;
    bool mouseYNegative_;
    u8 mouseDeltaX_;
    u8 mouseDeltaY_;
    bool lightGunButtons_[5];
    bool scopeTurboSwitch_;
    bool scopeTurboReported_;
    bool scopeFirePulse_;
    bool scopePausePulse_;
    bool scopeFireSerial_;
    bool scopePauseSerial_;
    s16 lightGunX_[2];
    s16 lightGunY_[2];
    bool lightGunOffscreen_[2];
    u8 justifierLatchedGun_;
    u8 justifierNextGun_;
};

} // namespace snes

#endif
