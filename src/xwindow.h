#pragma once

#include "config.h"

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace ui
{

struct State;
enum class Event;

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

Event process_input_events(Display *display, State &state,
                           const Config &config);

} // namespace ui
