#include "utility.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo-xlib.h>
#include <cairo.h>
#include <pango/pangocairo.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

static constexpr int WIDTH = 600;
static constexpr int HEIGHT = 50;

namespace
{
void draw(Display *display, Window window, int width, int height)
{
    // Get the default visual
    int const screen = DefaultScreen(display);
    Visual *visual = DefaultVisual(display, screen);

    // Create Cairo surface for X11 window
    cairo_surface_t *surface =
        cairo_xlib_surface_create(display, window, visual, width, height);
    defer const cleanup_surface(
        [surface]() { cairo_surface_destroy(surface); });

    // Create Cairo context
    cairo_t *cr = cairo_create(surface);
    defer const cleanup_cr([cr]() { cairo_destroy(cr); });

    // Create Pango layout for text rendering
    PangoLayout *layout = pango_cairo_create_layout(cr);
    defer const cleanup_layout([layout]() { g_object_unref(layout); });

    // Clear background with white
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Draw some example text with different styles
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_move_to(cr, 10, 10);

    // Normal text
    pango_layout_set_text(layout, "Hello Cairo+Pango! ", -1);
    pango_cairo_show_layout(cr, layout);

    // Bold text
    PangoAttrList *attrs = pango_attr_list_new();
    defer const cleanup_attrs([attrs]() { pango_attr_list_unref(attrs); });

    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));

    pango_layout_set_text(layout, "Bold", -1);
    pango_layout_set_attributes(layout, attrs);
    pango_cairo_show_layout(cr, layout);

    // Colored text
    cairo_set_source_rgb(cr, 0.8, 0.0, 0.0); // Red
    PangoAttrList *attrs2 = pango_attr_list_new();
    defer const cleanup_attrs2([attrs2]() { pango_attr_list_unref(attrs2); });

    pango_attr_list_insert(attrs2,
                           pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));

    pango_layout_set_text(layout, " + Underlined Red", -1);
    pango_layout_set_attributes(layout, attrs2);
    pango_cairo_show_layout(cr, layout);

    // Show instructions
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_move_to(cr, 10, 30);
    pango_layout_set_text(layout, "(Press ESC to quit)", -1);
    pango_layout_set_attributes(layout, nullptr); // Clear attributes
    pango_cairo_show_layout(cr, layout);

    // Flush to display
    cairo_surface_flush(surface);
}

} // namespace

int main()
{
    // Open connection to X server
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "Cannot open display\n";
        return 1;
    }
    defer const cleanup_display([display]() { XCloseDisplay(display); });

    int const screen = DefaultScreen(display);

    // Get screen dimensions for centering
    int const screen_width = DisplayWidth(display, screen);
    int const screen_height = DisplayHeight(display, screen);

    int const x = (screen_width - WIDTH) / 2;
    int const y = screen_height / 4;

    // Create window attributes
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = WhitePixel(display, screen);
    attrs.border_pixel = BlackPixel(display, screen);
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask;

    // Create the window
    Window const window = XCreateWindow(
        display, RootWindow(display, screen), x, y, WIDTH, HEIGHT, 2,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &attrs);

    defer const cleanup_window(
        [display, window]() { XDestroyWindow(display, window); });

    // Set window type hint
    Atom const windowType = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom const dialogType =
        XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, window, windowType, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&dialogType, 1);

    // Set window to stay on top
    Atom const stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom const stateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(display, window, stateAtom, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&stateAbove, 1);

    // Map (show) the window
    XMapRaised(display, window);

    // Grab keyboard focus
    XSetInputFocus(display, window, RevertToParent, CurrentTime);

    XFlush(display);

    std::cout << "Launcher window opened. Press ESC to close.\n";

    // Event loop
    XEvent event;
    bool running = true;

    while (running) {
        XNextEvent(display, &event);

        switch (event.type) {
        case Expose:
            if (event.xexpose.count == 0) {
                draw(display, window, WIDTH, HEIGHT);
            }
            break;

        case KeyPress: {
            KeySym const keysym = XLookupKeysym(&event.xkey, 0);

            if (keysym == XK_Escape) {
                running = false;
            } else {
                // Just print for now
                char buffer[32];
                int const len = XLookupString(&event.xkey, buffer,
                                              sizeof(buffer), nullptr, nullptr);
                if (len > 0) {
                    buffer[len] = '\0';
                    std::cout << "Key pressed: " << buffer << "\n";
                }
            }
            break;
        }
        }
    }

    return 0;
}