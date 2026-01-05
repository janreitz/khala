#pragma once

#include "config.h"
#include "ui.h"

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cairo-xlib.h>
#include <cairo.h>

#include <variant>
#include <vector>

struct XWindow {
    Display *display = nullptr;
    ::Window window = 0;
    Colormap colormap = 0;
    int width = 0;
    unsigned int height = 0;
    unsigned int screen_height = 0;

    XWindow(ui::RelScreenCoord top_left, ui::RelScreenCoord dimension);
    ~XWindow();

    void resize(unsigned int height, unsigned int width);
    cairo_surface_t* create_cairo_surface(unsigned int height, unsigned int width);

    // Non-copyable
    XWindow(const XWindow &) = delete;
    XWindow &operator=(const XWindow &) = delete;
};

std::vector<ui::UserInputEvent> get_input_events(Display *display,
                                                 bool blocking = true);
