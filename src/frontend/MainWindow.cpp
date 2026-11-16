// Created by Jacob Hodgkins
#include "MainWindow.hpp"
#include <cairo.h>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
#include <utility>

namespace snes {
namespace {
GdkPixbuf* loadCipherSNESBrand(int size){
    std::vector<std::filesystem::path> candidates;
    if(char* base=SDL_GetBasePath()){
        const std::filesystem::path executable=std::filesystem::u8path(base);
        candidates.push_back(executable/"../Resources/CipherSNES_icon.png"); // macOS .app
        candidates.push_back(executable/"../share/ciphersnes/CipherSNES_icon.png"); // Flatpak/Linux
        candidates.push_back(executable/"../assets/CipherSNES_icon.png"); // local build
        candidates.push_back(executable/"assets/CipherSNES_icon.png"); // Windows bundle
        SDL_free(base);
    }
    candidates.emplace_back("assets/CipherSNES_icon.png");
    for(const auto& file:candidates){
        GError* err=nullptr;
        GdkPixbuf* pix=gdk_pixbuf_new_from_file_at_scale(file.string().c_str(),size,size,TRUE,&err);
        if(pix)return pix;
        if(err)g_error_free(err);
    }
    g_warning("CipherSNES branded icon could not be located");
    return nullptr;
}
} // namespace
MainWindow::MainWindow() {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "CipherSNES (No ROM loaded)");
    if(GdkPixbuf* icon=loadCipherSNESBrand(256)){
        gtk_window_set_default_icon(icon);
        gtk_window_set_icon(GTK_WINDOW(window_),icon);
        g_object_unref(icon);
    }
    gtk_window_set_default_size(GTK_WINDOW(window_), 768, 608);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), root);
    GtkWidget* bar = gtk_menu_bar_new();
    gtk_box_pack_start(GTK_BOX(root), bar, FALSE, FALSE, 0);
    GtkWidget* file = gtk_menu_item_new_with_label("File");
    GtkWidget* config = gtk_menu_item_new_with_label("Config");
    GtkWidget* help = gtk_menu_item_new_with_label("Help");
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), file);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), config);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), help);
    GtkWidget* fileMenu = gtk_menu_new();
    GtkWidget* configMenu = gtk_menu_new();
    GtkWidget* helpMenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), fileMenu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(config), configMenu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(help), helpMenu);
    GtkWidget* load = gtk_menu_item_new_with_label("Load SNES ROM");
    resetMenu_ = gtk_menu_item_new_with_label("Reset");
    GtkWidget* quit = gtk_menu_item_new_with_label("Quit");
    GtkWidget* controller = gtk_menu_item_new_with_label("Controller");
    GtkWidget* about = gtk_menu_item_new_with_label("About CipherSNES...");
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), load);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), resetMenu_);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), quit);
    gtk_menu_shell_append(GTK_MENU_SHELL(configMenu), controller);
    gtk_menu_shell_append(GTK_MENU_SHELL(helpMenu), about);
    gtk_widget_set_sensitive(resetMenu_, FALSE);
    g_signal_connect(load, "activate", G_CALLBACK(menuLoad), this);
    g_signal_connect(resetMenu_, "activate", G_CALLBACK(menuReset), this);
    g_signal_connect(quit, "activate", G_CALLBACK(menuQuit), this);
    g_signal_connect(controller, "activate", G_CALLBACK(menuController), this);
    g_signal_connect(about, "activate", G_CALLBACK(menuAbout), this);
    drawingArea_ = gtk_drawing_area_new();
    gtk_widget_set_hexpand(drawingArea_, TRUE);
    gtk_widget_set_vexpand(drawingArea_, TRUE);
    gtk_box_pack_start(GTK_BOX(root), drawingArea_, TRUE, TRUE, 0);
    g_signal_connect(drawingArea_, "draw", G_CALLBACK(draw), this);
    gtk_widget_add_events(window_, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(keyDown), this);
    g_signal_connect(window_, "key-release-event", G_CALLBACK(keyUp), this);
    g_signal_connect(window_, "focus-out-event", G_CALLBACK(lostFocus), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(destroyed), this);
    // Queue-based SDL audio keeps sound independent from GTK rendering.
    // No callbacks touch emulation state, and audio-device absence is harmless.
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)==0){
        audioInitialized_=true;
        SDL_AudioSpec desired{};
        desired.freq=static_cast<int>(Core::AudioSampleRate);
        desired.format=AUDIO_S16SYS;
        desired.channels=2;
        desired.samples=1024;
        audioDevice_=SDL_OpenAudioDevice(nullptr,0,&desired,nullptr,0);
        if(audioDevice_)SDL_PauseAudioDevice(audioDevice_,0);
        else g_warning("Audio device unavailable: %s",SDL_GetError());
    }else g_warning("SDL audio subsystem unavailable: %s",SDL_GetError());
    // CP56: one-shot adaptive GTK wake; no 500Hz fixed callback tax.
    timer_ = g_timeout_add(1, tick, this);
}
MainWindow::~MainWindow() { invalidateVideoSurface(); }
void MainWindow::invalidateVideoSurface() {
    if (videoSurface_) cairo_surface_destroy(videoSurface_);
    videoSurface_ = nullptr;
    videoSurfacePixels_ = nullptr;
    videoSurfaceWidth_ = videoSurfaceHeight_ = videoSurfacePitch_ = 0;
}
void MainWindow::run() { gtk_widget_show_all(window_); gtk_main(); }
void MainWindow::showError(const std::string& error) {
    GtkWidget* box = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error.c_str());
    gtk_dialog_run(GTK_DIALOG(box));
    gtk_widget_destroy(box);
}
void MainWindow::flushSram(){
    std::string error;
    if(!system_.save(error))showError(error);
}
void MainWindow::loadRom() {
    // GTK implements the platform's native file chooser where available.
    GtkFileChooserNative* chooser = gtk_file_chooser_native_new(
        "Load SNES ROM", GTK_WINDOW(window_), GTK_FILE_CHOOSER_ACTION_OPEN, "Open", "Cancel");
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "SNES ROMs (*.sfc, *.smc, *.fig, *.swc, *.bin, *.zip)");
    for (const char* ext : {"*.sfc", "*.smc", "*.fig", "*.swc", "*.bin", "*.zip",
                            "*.SFC", "*.SMC", "*.FIG", "*.SWC", "*.BIN", "*.ZIP"})
        gtk_file_filter_add_pattern(filter, ext);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
    GtkFileFilter* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all);
    const gint response = gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser));
    if (response == GTK_RESPONSE_ACCEPT) {
        char* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (file) {
            std::string error;
            // GLib selects the correct per-user application data directory on
            // Linux, Windows and macOS (including a Flatpak sandbox).
            std::filesystem::path base=std::filesystem::u8path(g_get_user_data_dir());
            base/="HodgkinsStudios/CipherSNES";
            invalidateVideoSurface();
            if(system_.loadRom(file,(base/"saves").string(),error)){
                std::filesystem::path iplPath;
                if(const char* env=std::getenv("CIPHERSNES_IPL_PATH"))iplPath=env;
                else iplPath=base/"firmware"/"spc700.ipl";
                if(!iplPath.empty() && std::filesystem::is_regular_file(iplPath) &&
                   !system_.loadIplFile(iplPath.string(),error))showError(error);
                cpuStatusShown_ = false;
                pacer_.configure(system_.isPal(), static_cast<std::int64_t>(g_get_monotonic_time())*1000);
                speedSampleStartNs_ = 0; speedSampleFrames_ = 0; latencyMeter_.reset();
                if(audioDevice_)SDL_ClearQueuedAudio(audioDevice_);
                const auto info = system_.cartridgeInfo();
                gtk_widget_set_sensitive(resetMenu_, TRUE);
                const std::string title = "CipherSNES - " + info.title;
                gameTitle_ = title;
                gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
                // CP53: do not block the emulation thread behind a modal
                // "ROM loaded" dialog. The title is already set, and the
                // next paced frame now appears without an extra click.
            } else showError(error);
            g_free(file);
        }
    }
    g_object_unref(chooser);
}
void MainWindow::resetSystem() {
    if (!system_.loaded()) return;
    std::string error;
    invalidateVideoSurface();
    if(!system_.reset(error)){showError(error);return;}
    if(audioDevice_)SDL_ClearQueuedAudio(audioDevice_);
    pacer_.configure(system_.isPal(), static_cast<std::int64_t>(g_get_monotonic_time())*1000);
    speedSampleStartNs_ = 0; speedSampleFrames_ = 0; latencyMeter_.reset();
    input_.clearKeyboard();
    cpuStatusShown_ = false;
}
void MainWindow::openAbout(){
    GtkWidget* dialog=gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog),"CipherSNES");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog),"0.1.0");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
        "C++17 Super Nintendo emulator for Linux, Windows and macOS.\nCreated by HodgkinsStudios.\nHardware accuracy is still under development.");
    if(GdkPixbuf* pix=loadCipherSNESBrand(256)){
        gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog),pix);
        gtk_window_set_icon(GTK_WINDOW(dialog),pix);
        g_object_unref(pix);
    }
    gtk_window_set_transient_for(GTK_WINDOW(dialog),GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dialog),TRUE);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
void MainWindow::openController() {
    if (dialog_) return;
    paused_ = true;
    if(audioDevice_){SDL_PauseAudioDevice(audioDevice_,1);SDL_ClearQueuedAudio(audioDevice_);}
    system_.discardAudio();
    input_.clearKeyboard();
    dialog_ = std::make_unique<ControllerDialog>(GTK_WINDOW(window_), input_, [this](ControllerSettings settings) {
        input_.apply(settings); // Return uses current working bindings, even if not saved to disk.
        dialog_.reset();
        paused_ = false;
        pacer_.reset(static_cast<std::int64_t>(g_get_monotonic_time())*1000);
        speedSampleStartNs_ = 0; speedSampleFrames_ = 0; latencyMeter_.reset();
        if(audioDevice_)SDL_PauseAudioDevice(audioDevice_,0);
    });
    gtk_widget_show_all(dialog_->widget());
}
gboolean MainWindow::draw(GtkWidget* widget, cairo_t* context, gpointer userdata) {
    auto* self=static_cast<MainWindow*>(userdata);
    cairo_set_source_rgb(context,0,0,0);cairo_paint(context);
    if(!self->system_.loaded())return FALSE;
    const auto image=self->system_.video();
    const unsigned nativeWidth=image.width;
    // CP56: Cairo image surfaces are lightweight views over Core-owned video.
    // Reuse them for unchanged geometry/pointer; recreate after reallocations
    // (hires/interlace/load/reset). The GTK/emulation loop is single-threaded.
    if (image.pixels != self->videoSurfacePixels_ ||
        image.width != self->videoSurfaceWidth_ ||
        image.height != self->videoSurfaceHeight_ ||
        image.pitchBytes != self->videoSurfacePitch_) {
        self->invalidateVideoSurface();
        self->videoSurface_ = cairo_image_surface_create_for_data(
            reinterpret_cast<unsigned char*>(const_cast<std::uint32_t*>(image.pixels)),
            CAIRO_FORMAT_RGB24,static_cast<int>(image.width),static_cast<int>(image.height),
            static_cast<int>(image.pitchBytes));
        self->videoSurfacePixels_ = image.pixels;
        self->videoSurfaceWidth_ = image.width;
        self->videoSurfaceHeight_ = image.height;
        self->videoSurfacePitch_ = image.pitchBytes;
    }
    cairo_surface_mark_dirty(self->videoSurface_);
    const int w=gtk_widget_get_allocated_width(widget),h=gtk_widget_get_allocated_height(widget);
    const double scale=std::min(w/double(nativeWidth),h/224.0);
    cairo_translate(context,(w-nativeWidth*scale)/2,(h-224*scale)/2);
    cairo_scale(context,scale,scale);
    cairo_set_source_surface(context,self->videoSurface_,0,0);
    cairo_pattern_set_filter(cairo_get_source(context),CAIRO_FILTER_NEAREST);
    cairo_paint(context);
    return FALSE;
}
gboolean MainWindow::keyDown(GtkWidget*, GdkEventKey* event, gpointer userdata) {
    auto* self = static_cast<MainWindow*>(userdata);
    if (!self->paused_) self->input_.keyPressed(event->keyval);
    return FALSE;
}
gboolean MainWindow::keyUp(GtkWidget*, GdkEventKey* event, gpointer userdata) {
    auto* self = static_cast<MainWindow*>(userdata);
    self->input_.keyReleased(event->keyval);
    return FALSE;
}
gboolean MainWindow::lostFocus(GtkWidget*, GdkEventFocus*, gpointer userdata) {
    static_cast<MainWindow*>(userdata)->input_.clearKeyboard();
    return FALSE;
}
gboolean MainWindow::tick(gpointer userdata) {
    auto* self = static_cast<MainWindow*>(userdata);
    self->input_.poll([self](PadBinding pad) {
        if (self->dialog_ && self->dialog_->isCapturingGamepad()) self->dialog_->onGamepadCapture(pad);
    });
    if (self->dialog_) self->dialog_->refreshStatus();
    if (!self->paused_ && self->system_.loaded())
        self->input_.updateSnesInput(self->system_);
    // Region-correct, monotonic-clock frame pacing (NTSC ~60.099, PAL ~50.007).
    // Execute ALL emulated frames when recovering from a short UI stall;
    // render only the latest frame to avoid redundant GTK paints. When the
    // host truly cannot sustain the target, bound catch-up to keep input live.
    if (!self->paused_ && self->system_.loaded() && !self->system_.faulted() &&
        !self->system_.stopped()) {
        const auto nowNs=static_cast<std::int64_t>(g_get_monotonic_time())*1000;
        const unsigned due=self->pacer_.framesDue(nowNs);
        bool presented=false;
        for(unsigned frame=0;frame<due;++frame){
            // Keep emulating sound and logic when minimized, but do not waste
            // CPU compositing frames that GTK cannot display.
            const bool present=frame+1==due && gtk_widget_get_mapped(self->drawingArea_);
            const auto before=self->system_.frameCount();
            const auto frameStartUs=g_get_monotonic_time();
            self->system_.runFrame(250000,present);
            const auto frameDurationUs=g_get_monotonic_time()-frameStartUs;
            self->latencyMeter_.addUs(static_cast<std::uint32_t>(std::min<gint64>(
                frameDurationUs, 10000000)));
            if(self->system_.frameCount()==before)break;
            self->pacer_.completedFrame();
            ++self->speedSampleFrames_;
            self->system_.takeAudioInto(self->audioBuffer_);
            if(self->audioDevice_ && !self->audioBuffer_.empty()){
                // Avoid piling up >~200 ms of stale sound after a long stall.
                constexpr Uint32 maxQueued=Core::AudioSampleRate*2u*sizeof(std::int16_t)/5u;
                if(SDL_GetQueuedAudioSize(self->audioDevice_)>maxQueued)
                    SDL_ClearQueuedAudio(self->audioDevice_);
                SDL_QueueAudio(self->audioDevice_,self->audioBuffer_.data(),
                    static_cast<Uint32>(self->audioBuffer_.size()*sizeof(std::int16_t)));
            }
            presented|=present;
            if(self->system_.faulted() || self->system_.stopped())break;
        }
        if(presented)gtk_widget_queue_draw(self->drawingArea_);
        if(self->speedSampleStartNs_==0)self->speedSampleStartNs_=nowNs;
        if(nowNs-self->speedSampleStartNs_>=1000000000LL){
            const double secs=(nowNs-self->speedSampleStartNs_)/1000000000.0;
            const double actual=self->speedSampleFrames_/secs;
            const double expected=self->system_.isPal()?50.0069789:60.0988139;
            const unsigned percent=static_cast<unsigned>(std::min(100.0,100.0*actual/expected+0.5));
            // Show real CPU frame-time tail latency rather than FPS alone.
            // p95 above ~16.6ms NTSC / 20ms PAL indicates weak headroom.
            const double p95ms=self->latencyMeter_.percentile(95)/1000.0;
            char suffix[96];
            std::snprintf(suffix,sizeof(suffix),
                " — %u%% (%.1f FPS, p95 %.1f ms)",percent,actual,p95ms);
            const std::string title=self->gameTitle_+suffix;
            gtk_window_set_title(GTK_WINDOW(self->window_),title.c_str());
            self->speedSampleStartNs_=nowNs;
            self->speedSampleFrames_=0;
        }
    }
    if (!self->cpuStatusShown_ && self->system_.faulted()) {
        auto title = std::string("CipherSNES - halted: ") + self->system_.faultReason();
        gtk_window_set_title(GTK_WINDOW(self->window_), title.c_str());
        self->cpuStatusShown_ = true;
    }
    // A one-shot source is reinstalled only when needed. This reduces idle
    // wakeups and avoids the old 500 callbacks/second on low-end CPUs.
    const auto scheduleNowNs=static_cast<std::int64_t>(g_get_monotonic_time())*1000;
    const bool running=!self->paused_ && self->system_.loaded() &&
        !self->system_.faulted() && !self->system_.stopped();
    const unsigned delay=snes::FrontendWake::delayMs(scheduleNowNs,
        self->pacer_.nextFrameNs(),running,self->dialog_!=nullptr);
    self->timer_=g_timeout_add(delay,tick,self);
    return G_SOURCE_REMOVE;
}
void MainWindow::menuLoad(GtkMenuItem*, gpointer data) { static_cast<MainWindow*>(data)->loadRom(); }
void MainWindow::menuReset(GtkMenuItem*, gpointer data) { static_cast<MainWindow*>(data)->resetSystem(); }
void MainWindow::menuQuit(GtkMenuItem*, gpointer data) {
    gtk_widget_destroy(static_cast<MainWindow*>(data)->window_);
}
void MainWindow::menuController(GtkMenuItem*, gpointer data) { static_cast<MainWindow*>(data)->openController(); }
void MainWindow::menuAbout(GtkMenuItem*, gpointer data) { static_cast<MainWindow*>(data)->openAbout(); }
void MainWindow::destroyed(GtkWidget*, gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    self->flushSram();
    if(self->audioDevice_){SDL_CloseAudioDevice(self->audioDevice_);self->audioDevice_=0;}
    if(self->audioInitialized_){SDL_QuitSubSystem(SDL_INIT_AUDIO);self->audioInitialized_=false;}
    if (self->timer_) { g_source_remove(self->timer_); self->timer_ = 0; }
    gtk_main_quit();
}
}
