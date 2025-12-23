#include "utility.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdlib>
#include <cstring>
#include <iostream>

static constexpr int HEIGHT = 50;
static constexpr int WIDTH = 600;

void draw(Display *display, Window window, GC gc)
{
    XClearWindow(display, window);

    const char *text = "Hello X11! Type something... (ESC to quit)";
    int text_len = strlen(text);

    int text_x = 10;
    int text_y = HEIGHT / 2 + 5;

    XDrawString(display, window, gc, text_x, text_y, text, text_len);

    XFlush(display);
}

int main()
{
    // Open connection to X server
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "Cannot open display";
        return 1;
    }
    const defer cleanup_display([display]() { XCloseDisplay(display); });

    // TODO: Get the screen the mouse is on
    int screen = DefaultScreen(display);

    // Get screen dimensions for centering
    int screen_width = DisplayWidth(display, screen);
    int screen_height = DisplayHeight(display, screen);

    int x = (screen_width - WIDTH) / 2;
    int y = screen_height / 4;

    // Create window attributes
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = WhitePixel(display, screen);
    attrs.border_pixel = BlackPixel(display, screen);
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask;

    // Create the window
    Window window = XCreateWindow(
        display, RootWindow(display, screen), x, y, WIDTH, HEIGHT, 2,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &attrs);

    const defer cleanup_window(
        [display, window]() { XDestroyWindow(display, window); });

    // Set window type hint
    Atom windowType = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom dialogType = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, window, windowType, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&dialogType, 1);

    // Set window to stay on top
    Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom stateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(display, window, stateAtom, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&stateAbove, 1);

    // Create graphics context
    GC gc = XCreateGC(display, window, 0, nullptr);
    const defer cleanup_gc([display, gc]() { XFreeGC(display, gc); });

    XSetForeground(display, gc, BlackPixel(display, screen));
    XSetBackground(display, gc, WhitePixel(display, screen));

    // Map (show) the window
    XMapRaised(display, window);

    // Grab keyboard focus
    XSetInputFocus(display, window, RevertToParent, CurrentTime);

    XFlush(display);

    std::cout << "Launcher window opened. Press ESC to close, type to see key "
                 "events.\n";

    XEvent event;
    bool running = true;
    while (running) {
        XNextEvent(display, &event);

        switch (event.type) {
        case Expose:
            if (event.xexpose.count == 0) {
                draw(display, window, gc);
            }
            break;

        case KeyPress: {
            KeySym keysym = XLookupKeysym(&event.xkey, 0);

            if (keysym == XK_Escape) {
                running = false;
            } else {
                char buffer[32];
                int len = XLookupString(&event.xkey, buffer, sizeof(buffer),
                                        nullptr, nullptr);
                if (len > 0) {
                    buffer[len] = '\0';
                    std::cout << "Key pressed: " << buffer
                              << " (keysym: " << keysym << ")\n";
                }
            }
            break;
        }
        }
    }

    return 0;
}