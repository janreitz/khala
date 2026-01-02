#pragma once

#include "config.h"
#include "ui.h"

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <variant>
#include <vector>


struct XWindow {
    Display *display = nullptr;
    ::Window window = 0;
    Colormap colormap = 0;
    int width = 0;
    int height = 0;
    int screen_height = 0;

    XWindow(const Config &config);
    ~XWindow();

    // Non-copyable
    XWindow(const XWindow &) = delete;
    XWindow &operator=(const XWindow &) = delete;
};

std::vector<ui::UserInputEvent> get_input_events(Display *display, bool blocking = true);
