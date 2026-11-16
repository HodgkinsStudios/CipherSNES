// Created by Jacob Hodgkins
#pragma once
#include "InputManager.hpp"
#include <gtk/gtk.h>
#include <array>
#include <functional>
#include <string>
namespace snes {
class ControllerDialog {
public:
    ControllerDialog(GtkWindow* parent, InputManager& input,
                     std::function<void(ControllerSettings)> onReturn);
    GtkWidget* widget() const { return window_; }
    bool isCapturingGamepad() const { return captureIndex_ >= 0 && capturePad_; }
    void onGamepadCapture(PadBinding binding);
    void refreshStatus();
private:
    void refreshBindings();
    void beginCapture(unsigned index, bool pad);
    void close();
    static gboolean onKey(GtkWidget*, GdkEventKey*, gpointer);
    static void onDestroy(GtkWidget*, gpointer);
    static void onReset(GtkButton*, gpointer);
    static void onSave(GtkButton*, gpointer);
    static void onReturnClick(GtkButton*, gpointer);
    static void onCapture(GtkButton*, gpointer);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer);
    GtkWidget* window_ = nullptr;
    GtkWidget* status_ = nullptr;
    GtkWidget* instructions_ = nullptr;
    std::array<GtkWidget*, buttonCount> keyButtons_{};
    std::array<GtkWidget*, buttonCount> padButtons_{};
    InputManager& input_;
    ControllerSettings working_;
    std::function<void(ControllerSettings)> onReturn_;
    int captureIndex_ = -1;
    bool capturePad_ = false;
};
}
