// Created by Jacob Hodgkins
#include "InputManager.hpp"
#include <glib.h>
#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <climits>
#include <sstream>
#include <utility>

namespace snes {
namespace {
constexpr const char* NAMES[] = {"B", "Y", "Select", "Start", "Up", "Down", "Left", "Right", "A", "X", "L", "R"};
constexpr int STICK_DEADZONE = 16000;
constexpr int CAPTURE_THRESHOLD = 24000;
}
const char* snesButtonName(unsigned index) { return index < buttonCount ? NAMES[index] : "?"; }
ControllerSettings InputManager::defaults() {
    ControllerSettings settings;
    const guint keys[] = {GDK_KEY_z, GDK_KEY_a, GDK_KEY_Shift_R, GDK_KEY_Return,
        GDK_KEY_Up, GDK_KEY_Down, GDK_KEY_Left, GDK_KEY_Right,
        GDK_KEY_x, GDK_KEY_s, GDK_KEY_q, GDK_KEY_w};
    const SDL_GameControllerButton pads[] = {
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
    for (unsigned i = 0; i < buttonCount; ++i) {
        settings.bindings[i].key = keys[i];
        settings.bindings[i].pad = {PadBinding::Type::Button, static_cast<int>(pads[i]), 1};
    }
    return settings;
}
std::string InputManager::configPath() {
    gchar* path = g_build_filename(g_get_user_config_dir(), "HodgkinsStudios", "CipherSNES", "controller.ini", nullptr);
    std::string s(path);
    g_free(path);
    return s;
}
InputManager::InputManager() : settings_(defaults()) {
    load();
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) == 0) {
        sdlOk_ = true;
        SDL_GameControllerEventState(SDL_ENABLE);
        findController();
    } else g_warning("Gamepad initialization failed: %s", SDL_GetError());
}
InputManager::~InputManager() {
    if (controller_) SDL_GameControllerClose(controller_);
    if (sdlOk_) SDL_Quit();
}
void InputManager::load() {
    GKeyFile* keyFile = g_key_file_new();
    const auto path = configPath();
    if (g_key_file_load_from_file(keyFile, path.c_str(), G_KEY_FILE_NONE, nullptr)) {
        auto draft = defaults();
        for (unsigned i = 0; i < buttonCount; ++i) {
            const std::string section = NAMES[i];
            auto get = [&](const char* field, int fallback) -> int {
                GError* err = nullptr;
                int value = g_key_file_get_integer(keyFile, section.c_str(), field, &err);
                if (err) { g_error_free(err); return fallback; }
                return value;
            };
            auto& b = draft.bindings[i];
            int key = get("key", static_cast<int>(b.key));
            int type = get("pad_type", 0);
            int control = get("pad_control", b.pad.control);
            int direction = get("pad_direction", 1);
            if (key >= 0) b.key = static_cast<guint>(key);
            if (type == 0 && control >= 0 && control < SDL_CONTROLLER_BUTTON_MAX)
                b.pad = {PadBinding::Type::Button, control, 1};
            else if (type == 1 && control >= 0 && control < SDL_CONTROLLER_AXIS_MAX && direction != 0)
                b.pad = {PadBinding::Type::Axis, control, direction < 0 ? -1 : 1};
        }
        settings_ = draft;
    }
    g_key_file_free(keyFile);
}
bool InputManager::save(const ControllerSettings& settings, std::string& error) const {
    GKeyFile* keyFile = g_key_file_new();
    for (unsigned i = 0; i < buttonCount; ++i) {
        const auto& b = settings.bindings[i];
        const auto section = NAMES[i];
        g_key_file_set_integer(keyFile, section, "key", static_cast<int>(b.key));
        g_key_file_set_integer(keyFile, section, "pad_type", static_cast<int>(b.pad.type));
        g_key_file_set_integer(keyFile, section, "pad_control", b.pad.control);
        g_key_file_set_integer(keyFile, section, "pad_direction", b.pad.direction);
    }
    gsize length = 0;
    gchar* data = g_key_file_to_data(keyFile, &length, nullptr);
    g_key_file_free(keyFile);
    const auto path = configPath();
    gchar* dirname = g_path_get_dirname(path.c_str());
    if (g_mkdir_with_parents(dirname, 0700) != 0) {
        error = "Could not create settings directory: " + std::string(dirname);
        g_free(dirname); g_free(data); return false;
    }
    g_free(dirname);
    GError* gerr = nullptr;
    gboolean ok = g_file_set_contents(path.c_str(), data, static_cast<gssize>(length), &gerr);
    g_free(data);
    if (!ok) {
        error = gerr ? gerr->message : "Could not save controller settings";
        if (gerr) g_error_free(gerr);
        return false;
    }
    return true;
}
void InputManager::apply(const ControllerSettings& settings) { settings_ = settings; clearKeyboard(); }
void InputManager::keyPressed(guint keyval) { keys_.insert(gdk_keyval_to_lower(keyval)); }
void InputManager::keyReleased(guint keyval) { keys_.erase(gdk_keyval_to_lower(keyval)); }
void InputManager::clearKeyboard() { keys_.clear(); }
void InputManager::findController() {
    if (!sdlOk_ || controller_) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        controller_ = SDL_GameControllerOpen(i);
        if (controller_) {
            instance_ = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller_));
            return;
        }
    }
}
void InputManager::poll(const std::function<void(PadBinding)>& capture) {
    if (!sdlOk_) return;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_CONTROLLERDEVICEADDED:
            findController();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (event.cdevice.which == instance_) {
                SDL_GameControllerClose(controller_);
                controller_ = nullptr;
                instance_ = -1;
                findController();
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (controller_ && event.cbutton.which == instance_ && capture)
                capture({PadBinding::Type::Button, static_cast<int>(event.cbutton.button), 1});
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (controller_ && event.caxis.which == instance_ && capture &&
                (event.caxis.value > CAPTURE_THRESHOLD || event.caxis.value < -CAPTURE_THRESHOLD))
                capture({PadBinding::Type::Axis, static_cast<int>(event.caxis.axis),
                         event.caxis.value < 0 ? -1 : 1});
            break;
        default: break;
        }
    }
    if (!controller_) findController();
}
void InputManager::updateSnesInput(Core& input) const {
    for (unsigned i = 0; i < buttonCount; ++i) {
        const auto& binding = settings_.bindings[i];
        bool down = binding.key != 0 && keys_.find(gdk_keyval_to_lower(binding.key)) != keys_.end();
        if (controller_) {
            if (binding.pad.type == PadBinding::Type::Button && binding.pad.control >= 0 &&
                binding.pad.control < SDL_CONTROLLER_BUTTON_MAX)
                down |= SDL_GameControllerGetButton(controller_, static_cast<SDL_GameControllerButton>(binding.pad.control)) != 0;
            if (binding.pad.type == PadBinding::Type::Axis && binding.pad.control >= 0 &&
                binding.pad.control < SDL_CONTROLLER_AXIS_MAX) {
                const int value = SDL_GameControllerGetAxis(controller_, static_cast<SDL_GameControllerAxis>(binding.pad.control));
                down |= binding.pad.direction < 0 ? value < -STICK_DEADZONE : value > STICK_DEADZONE;
            }
            // Left-stick directional fallback when using the default D-pad bindings.
            if (binding.pad.type == PadBinding::Type::Button &&
                ((i == static_cast<unsigned>(SnesButton::Left) && binding.pad.control == SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                 (i == static_cast<unsigned>(SnesButton::Right) && binding.pad.control == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                 (i == static_cast<unsigned>(SnesButton::Up) && binding.pad.control == SDL_CONTROLLER_BUTTON_DPAD_UP) ||
                 (i == static_cast<unsigned>(SnesButton::Down) && binding.pad.control == SDL_CONTROLLER_BUTTON_DPAD_DOWN))) {
                int axis = -1, direction = 0;
                if (i == static_cast<unsigned>(SnesButton::Left)) { axis = SDL_CONTROLLER_AXIS_LEFTX; direction = -1; }
                if (i == static_cast<unsigned>(SnesButton::Right)) { axis = SDL_CONTROLLER_AXIS_LEFTX; direction = 1; }
                if (i == static_cast<unsigned>(SnesButton::Up)) { axis = SDL_CONTROLLER_AXIS_LEFTY; direction = -1; }
                if (i == static_cast<unsigned>(SnesButton::Down)) { axis = SDL_CONTROLLER_AXIS_LEFTY; direction = 1; }
                if (axis >= 0) {
                    int value = SDL_GameControllerGetAxis(controller_, static_cast<SDL_GameControllerAxis>(axis));
                    down |= direction < 0 ? value < -STICK_DEADZONE : value > STICK_DEADZONE;
                }
            }
        }
        input.setButton(0, static_cast<SnesButton>(i), down);
    }
}
std::string InputManager::controllerStatus() const {
    if (!sdlOk_) return "Gamepad subsystem unavailable (keyboard still works)";
    if (!controller_) return "No gamepad connected - reconnect anytime";
    const char* name = SDL_GameControllerName(controller_);
    return std::string("Connected: ") + (name ? name : "Controller");
}
std::string InputManager::describeKey(guint key) {
    if (!key) return "Unbound";
    const char* name = gdk_keyval_name(key);
    return name ? name : "Unknown key";
}
std::string InputManager::describePad(const PadBinding& p) {
    if (p.type == PadBinding::Type::Axis) {
        if (p.control < 0 || p.control >= SDL_CONTROLLER_AXIS_MAX) return "Invalid axis";
        const char* name = SDL_GameControllerGetStringForAxis(static_cast<SDL_GameControllerAxis>(p.control));
        return std::string(name ? name : "Axis") + (p.direction < 0 ? " (-)" : " (+)");
    }
    if (p.control < 0 || p.control >= SDL_CONTROLLER_BUTTON_MAX) return "Invalid button";
    const char* name = SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(p.control));
    return name ? name : "Button";
}
}
