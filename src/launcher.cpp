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
#include <string>

static constexpr int WIDTH = 600;
static constexpr int HEIGHT = 50;

namespace
{
void draw(Display *display, Window window, int width, int height, const std::string& search_buffer)
{
    // Get the default visual
    int const screen = DefaultScreen(display);
    Visual *visual = DefaultVisual(display, screen);

    // Create Cairo surface for X11 window
    cairo_surface_t *surface =
        cairo_xlib_surface_create(display, window, visual, width, height);
    defer const cleanup_surface(
        [surface]() noexcept { cairo_surface_destroy(surface); });

    // Create Cairo context
    cairo_t *cr = cairo_create(surface);
    defer const cleanup_cr([cr]() noexcept { cairo_destroy(cr); });

    // Create Pango layout for text rendering
    PangoLayout *layout = pango_cairo_create_layout(cr);
    defer const cleanup_layout([layout]() noexcept { g_object_unref(layout); });

    // Clear background with white
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Set font for launcher
    PangoFontDescription *font_desc = pango_font_description_from_string("Sans 12");
    defer const cleanup_font([font_desc]() noexcept { pango_font_description_free(font_desc); });
    pango_layout_set_font_description(layout, font_desc);

    // Draw search prompt and buffer
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_move_to(cr, 10, 15);
    
    std::string display_text = "> " + search_buffer;
    if (search_buffer.empty()) {
        display_text += "(type to search...)";
    }
    
    pango_layout_set_text(layout, display_text.c_str(), -1);
    pango_layout_set_attributes(layout, nullptr);
    pango_cairo_show_layout(cr, layout);

    // Draw cursor after text if there's content
    if (!search_buffer.empty()) {
        int text_width, text_height;
        pango_layout_get_size(layout, &text_width, &text_height);
        
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_move_to(cr, 10 + (text_width / PANGO_SCALE), 15);
        cairo_line_to(cr, 10 + (text_width / PANGO_SCALE), 15 + (text_height / PANGO_SCALE));
        cairo_stroke(cr);
    }

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
    defer const cleanup_display(
        [display]() noexcept { XCloseDisplay(display); });

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
        [display, window]() noexcept { XDestroyWindow(display, window); });

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
    std::string search_buffer;
    bool needs_redraw = true;

    while (running) {
        XNextEvent(display, &event);

        switch (event.type) {
        case Expose:
            if (event.xexpose.count == 0) {
                draw(display, window, WIDTH, HEIGHT, search_buffer);
            }
            break;

        case KeyPress: {
            KeySym const keysym = XLookupKeysym(&event.xkey, 0);

            if (keysym == XK_Escape) {
                running = false;
            } else if (keysym == XK_BackSpace) {
                // Handle backspace
                if (!search_buffer.empty()) {
                    search_buffer.pop_back();
                    needs_redraw = true;
                }
            } else {
                // Handle regular character input
                char char_buffer[32];
                int const len = XLookupString(&event.xkey, char_buffer,
                                              sizeof(char_buffer), nullptr, nullptr);
                if (len > 0) {
                    char_buffer[len] = '\0';
                    // Only add printable characters
                    for (int i = 0; i < len; ++i) {
                        if (char_buffer[i] >= 32 && char_buffer[i] < 127) {
                            search_buffer += char_buffer[i];
                            needs_redraw = true;
                        }
                    }
                    std::cout << "Search buffer: \"" << search_buffer << "\"\n";
                }
            }
            
            // Redraw if buffer changed
            if (needs_redraw) {
                draw(display, window, WIDTH, HEIGHT, search_buffer);
                needs_redraw = false;
            }
            break;
        }
        }
    }

    return 0;
}