// Created by Jacob Hodgkins
#include "MainWindow.hpp"
#include <gtk/gtk.h>
#include <cstdio>
int main(int argc, char** argv) {
    if (!gtk_init_check(&argc, &argv)) {
        std::fprintf(stderr, "Cannot initialize GTK. A desktop graphical session is required.\n");
        return 1;
    }
    snes::MainWindow frontend;
    frontend.run();
    return 0;
}
