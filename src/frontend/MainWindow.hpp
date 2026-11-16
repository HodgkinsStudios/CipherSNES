// Created by Jacob Hodgkins
#pragma once
#include "ControllerDialog.hpp"
#include "InputManager.hpp"
#include <CipherSNES/Core.hpp>
#include <CipherSNES/FramePacer.hpp>
#include <CipherSNES/FrontendWake.hpp>
#include <CipherSNES/FrameLatencyMeter.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <gtk/gtk.h>
#include <SDL.h>
#include <memory>
#include <filesystem>
namespace snes {
class MainWindow {
public:
    MainWindow();
    void run();
    ~MainWindow();
private:
    void loadRom();
    void resetSystem();
    void openController();
    void openAbout();
    void showError(const std::string& error);
    void flushSram();
    void invalidateVideoSurface();
    static gboolean draw(GtkWidget*, cairo_t*, gpointer);
    static gboolean keyDown(GtkWidget*, GdkEventKey*, gpointer);
    static gboolean keyUp(GtkWidget*, GdkEventKey*, gpointer);
    static gboolean lostFocus(GtkWidget*, GdkEventFocus*, gpointer);
    static gboolean tick(gpointer);
    static void menuLoad(GtkMenuItem*, gpointer);
    static void menuReset(GtkMenuItem*, gpointer);
    static void menuQuit(GtkMenuItem*, gpointer);
    static void menuController(GtkMenuItem*, gpointer);
    static void menuAbout(GtkMenuItem*, gpointer);
    static void destroyed(GtkWidget*, gpointer);
    GtkWidget* window_ = nullptr;
    GtkWidget* drawingArea_ = nullptr;
    GtkWidget* resetMenu_ = nullptr;
    Core system_;
    InputManager input_;
    std::unique_ptr<ControllerDialog> dialog_;
    bool paused_ = false;
    bool cpuStatusShown_ = false;
    guint timer_ = 0;
    cairo_surface_t* videoSurface_ = nullptr;
    const std::uint32_t* videoSurfacePixels_ = nullptr;
    unsigned videoSurfaceWidth_ = 0, videoSurfaceHeight_ = 0, videoSurfacePitch_ = 0;
    FramePacer pacer_;
    FrameLatencyMeter latencyMeter_;
    std::vector<std::int16_t> audioBuffer_;
    std::string gameTitle_;
    std::int64_t speedSampleStartNs_ = 0;
    unsigned speedSampleFrames_ = 0;
    SDL_AudioDeviceID audioDevice_ = 0;
    bool audioInitialized_ = false;
};
}
