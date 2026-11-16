// Created by Jacob Hodgkins
#pragma once
#include <CipherSNES/Core.hpp>
#include <SDL.h>
#include <gdk/gdk.h>
#include <array>
#include <functional>
#include <set>
#include <string>

namespace snes {
struct PadBinding {
    enum class Type { Button = 0, Axis = 1 };
    Type type = Type::Button;
    int control = 0;
    int direction = 1; // axis direction: -1 or +1
};
struct Binding { guint key = 0; PadBinding pad{}; };
struct ControllerSettings { std::array<Binding, buttonCount> bindings{}; };

class InputManager {
public:
    InputManager();
    ~InputManager();
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    static ControllerSettings defaults();
    const ControllerSettings& settings() const { return settings_; }
    void apply(const ControllerSettings& settings);
    bool save(const ControllerSettings& settings, std::string& error) const;
    void keyPressed(guint keyval);
    void keyReleased(guint keyval);
    void clearKeyboard();
    void poll(const std::function<void(PadBinding)>& capture);
    void updateSnesInput(Core& input) const;
    std::string controllerStatus() const;
    static std::string describeKey(guint key);
    static std::string describePad(const PadBinding& pad);
private:
    void load();
    void findController();
    static std::string configPath();
    ControllerSettings settings_{};
    std::set<guint> keys_;
    SDL_GameController* controller_ = nullptr;
    SDL_JoystickID instance_ = -1;
    bool sdlOk_ = false;
};
const char* snesButtonName(unsigned index);
}
