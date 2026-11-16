// Created by Jacob Hodgkins
#include "ControllerDialog.hpp"
#include <string>
#include <utility>

namespace snes {
namespace {
void errorBox(GtkWindow* parent, const std::string& msg) {
    GtkWidget* box = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_CLOSE, "%s", msg.c_str());
    gtk_dialog_run(GTK_DIALOG(box));
    gtk_widget_destroy(box);
}
}
ControllerDialog::ControllerDialog(GtkWindow* parent, InputManager& input,
                                   std::function<void(ControllerSettings)> onReturn)
    : input_(input), working_(input.settings()), onReturn_(std::move(onReturn)) {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "CipherSNES - Controller 1");
    gtk_window_set_transient_for(GTK_WINDOW(window_), parent);
    gtk_window_set_modal(GTK_WINDOW(window_), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window_), 570, 530);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_container_set_border_width(GTK_CONTAINER(window_), 12);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window_), root);
    GtkWidget* title = gtk_label_new("Controller 1 - Keyboard and Gamepad Bindings");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), title, FALSE, FALSE, 0);
    instructions_ = gtk_label_new("Select a binding to change it. Escape cancels capture.");
    gtk_widget_set_halign(instructions_, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), instructions_, FALSE, FALSE, 0);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(root), scroll, TRUE, TRUE, 0);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 7);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_container_add(GTK_CONTAINER(scroll), grid);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("SNES"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Keyboard"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Gamepad"), 2, 0, 1, 1);
    for (unsigned i = 0; i < buttonCount; ++i) {
        GtkWidget* name = gtk_label_new(snesButtonName(i));
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), name, 0, static_cast<int>(i + 1), 1, 1);
        keyButtons_[i] = gtk_button_new();
        padButtons_[i] = gtk_button_new();
        gtk_widget_set_can_focus(keyButtons_[i], FALSE);
        gtk_widget_set_can_focus(padButtons_[i], FALSE);
        gtk_widget_set_hexpand(keyButtons_[i], TRUE);
        gtk_widget_set_hexpand(padButtons_[i], TRUE);
        gtk_grid_attach(GTK_GRID(grid), keyButtons_[i], 1, static_cast<int>(i + 1), 1, 1);
        gtk_grid_attach(GTK_GRID(grid), padButtons_[i], 2, static_cast<int>(i + 1), 1, 1);
        g_object_set_data(G_OBJECT(keyButtons_[i]), "binding-index", GINT_TO_POINTER(i + 1));
        g_object_set_data(G_OBJECT(padButtons_[i]), "binding-index", GINT_TO_POINTER(i + 1));
        g_object_set_data(G_OBJECT(padButtons_[i]), "is-pad", GINT_TO_POINTER(1));
        g_signal_connect(keyButtons_[i], "clicked", G_CALLBACK(onCapture), this);
        g_signal_connect(padButtons_[i], "clicked", G_CALLBACK(onCapture), this);
    }
    status_ = gtk_label_new("");
    gtk_widget_set_halign(status_, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), status_, FALSE, FALSE, 0);
    GtkWidget* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(buttons), 8);
    gtk_box_pack_start(GTK_BOX(root), buttons, FALSE, FALSE, 0);
    GtkWidget* resetButton = gtk_button_new_with_label("Reset");
    GtkWidget* saveButton = gtk_button_new_with_label("Save");
    GtkWidget* returnButton = gtk_button_new_with_label("Return");
    gtk_container_add(GTK_CONTAINER(buttons), resetButton);
    gtk_container_add(GTK_CONTAINER(buttons), saveButton);
    gtk_container_add(GTK_CONTAINER(buttons), returnButton);
    g_signal_connect(resetButton, "clicked", G_CALLBACK(onReset), this);
    g_signal_connect(saveButton, "clicked", G_CALLBACK(onSave), this);
    g_signal_connect(returnButton, "clicked", G_CALLBACK(onReturnClick), this);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(onKey), this);
    g_signal_connect(window_, "delete-event", G_CALLBACK(onDelete), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(onDestroy), this);
    refreshBindings(); refreshStatus();
}
void ControllerDialog::refreshBindings() {
    for (unsigned i = 0; i < buttonCount; ++i) {
        gtk_button_set_label(GTK_BUTTON(keyButtons_[i]), InputManager::describeKey(working_.bindings[i].key).c_str());
        gtk_button_set_label(GTK_BUTTON(padButtons_[i]), InputManager::describePad(working_.bindings[i].pad).c_str());
    }
}
void ControllerDialog::refreshStatus() {
    if (window_) gtk_label_set_text(GTK_LABEL(status_), input_.controllerStatus().c_str());
}
void ControllerDialog::beginCapture(unsigned index, bool pad) {
    captureIndex_ = static_cast<int>(index);
    capturePad_ = pad;
    std::string text = std::string("Assign ") + snesButtonName(index) +
        (pad ? ": press a gamepad button or move a stick..." : ": press a keyboard key...");
    gtk_label_set_text(GTK_LABEL(instructions_), text.c_str());
    gtk_window_present(GTK_WINDOW(window_));
}
void ControllerDialog::onGamepadCapture(PadBinding binding) {
    if (!isCapturingGamepad()) return;
    working_.bindings[static_cast<unsigned>(captureIndex_)].pad = binding;
    captureIndex_ = -1;
    gtk_label_set_text(GTK_LABEL(instructions_), "Binding updated. Save to persist or Return to use temporarily.");
    refreshBindings();
}
gboolean ControllerDialog::onKey(GtkWidget*, GdkEventKey* event, gpointer userdata) {
    auto* self = static_cast<ControllerDialog*>(userdata);
    if (self->captureIndex_ < 0) return FALSE;
    if (event->keyval == GDK_KEY_Escape) {
        self->captureIndex_ = -1;
        gtk_label_set_text(GTK_LABEL(self->instructions_), "Capture cancelled.");
        return TRUE;
    }
    if (self->capturePad_) return TRUE;  // Don't remap pad binding from a keyboard event.
    self->working_.bindings[static_cast<unsigned>(self->captureIndex_)].key = gdk_keyval_to_lower(event->keyval);
    self->captureIndex_ = -1;
    gtk_label_set_text(GTK_LABEL(self->instructions_), "Binding updated. Save to persist or Return to use temporarily.");
    self->refreshBindings();
    return TRUE;
}
void ControllerDialog::onCapture(GtkButton* sender, gpointer userdata) {
    auto* self = static_cast<ControllerDialog*>(userdata);
    auto index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sender), "binding-index"));
    bool pad = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sender), "is-pad")) != 0;
    self->beginCapture(static_cast<unsigned>(index - 1), pad);
}
void ControllerDialog::onReset(GtkButton*, gpointer userdata) {
    auto* self = static_cast<ControllerDialog*>(userdata);
    self->working_ = InputManager::defaults();
    self->captureIndex_ = -1;
    self->refreshBindings();
    gtk_label_set_text(GTK_LABEL(self->instructions_), "Defaults restored in this window; Save to persist them.");
}
void ControllerDialog::onSave(GtkButton*, gpointer userdata) {
    auto* self = static_cast<ControllerDialog*>(userdata);
    std::string error;
    if (!self->input_.save(self->working_, error)) { errorBox(GTK_WINDOW(self->window_), error); return; }
    self->input_.apply(self->working_);
    gtk_label_set_text(GTK_LABEL(self->instructions_), "Saved to disk. Return to resume.");
}
void ControllerDialog::close() { gtk_widget_destroy(window_); }
void ControllerDialog::onReturnClick(GtkButton*, gpointer userdata) {
    static_cast<ControllerDialog*>(userdata)->close();
}
gboolean ControllerDialog::onDelete(GtkWidget*, GdkEvent*, gpointer userdata) {
    static_cast<ControllerDialog*>(userdata)->close();
    return TRUE;
}
void ControllerDialog::onDestroy(GtkWidget*, gpointer userdata) {
    auto* self = static_cast<ControllerDialog*>(userdata);
    // Make the callback last: the owner deletes this C++ wrapper when dialog closes.
    auto callback = self->onReturn_;
    auto settings = self->working_;
    self->window_ = nullptr;
    callback(settings);
}
}
