#include "xwindow.h"
#include "ui.h"
#include "utility.h"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <array>
#include <stdexcept>
#include <string>

namespace
{

struct MonitorInfo {
    int width;
    int height;
    int x;
    int y;
    bool found;
};

MonitorInfo get_primary_monitor_xrandr(Display *display, int screen)
{
    MonitorInfo info = {0, 0, 0, 0, false};

    // Check if XRandR extension is available
    int xrandr_event_base, xrandr_error_base;
    if (!XRRQueryExtension(display, &xrandr_event_base, &xrandr_error_base)) {
        printf("XRandR extension not available\n");
        return info;
    }

    // Check XRandR version
    int major_version, minor_version;
    if (!XRRQueryVersion(display, &major_version, &minor_version)) {
        printf("XRandR version query failed\n");
        return info;
    }

    printf("XRandR version: %d.%d\n", major_version, minor_version);

    // We need at least XRandR 1.2 for monitor info
    if (major_version < 1 || (major_version == 1 && minor_version < 2)) {
        printf("XRandR version too old (need 1.2+)\n");
        return info;
    }

    Window root = RootWindow(display, screen);

    // Get screen resources
    XRRScreenResources *screen_resources = XRRGetScreenResources(display, root);
    if (!screen_resources) {
        printf("Failed to get XRandR screen resources\n");
        return info;
    }

    // Find primary output
    RROutput primary = XRRGetOutputPrimary(display, root);

    // If no primary is set, use the first connected output
    if (primary == None) {
        printf("No primary output set, looking for first connected output\n");
        for (int i = 0; i < screen_resources->noutput; i++) {
            XRROutputInfo *output_info = XRRGetOutputInfo(
                display, screen_resources, screen_resources->outputs[i]);
            if (output_info && output_info->connection == RR_Connected &&
                output_info->crtc) {
                primary = screen_resources->outputs[i];
                XRRFreeOutputInfo(output_info);
                break;
            }
            if (output_info)
                XRRFreeOutputInfo(output_info);
        }
    }

    if (primary != None) {
        XRROutputInfo *output_info =
            XRRGetOutputInfo(display, screen_resources, primary);
        if (output_info && output_info->crtc) {
            XRRCrtcInfo *crtc_info =
                XRRGetCrtcInfo(display, screen_resources, output_info->crtc);
            if (crtc_info) {
                info.width = crtc_info->width;
                info.height = crtc_info->height;
                info.x = crtc_info->x;
                info.y = crtc_info->y;
                info.found = true;

                printf("Primary monitor found: %dx%d at (%d,%d)\n", info.width,
                       info.height, info.x, info.y);

                XRRFreeCrtcInfo(crtc_info);
            }
        }
        if (output_info)
            XRRFreeOutputInfo(output_info);
    }

    XRRFreeScreenResources(screen_resources);
    return info;
}

} // anonymous namespace

XWindow::XWindow(const Config &config)
{
    display = XOpenDisplay(nullptr);
    if (!display) {
        throw std::runtime_error("Cannot open display");
    }

    const int screen = DefaultScreen(display);

    // Get primary monitor info using XRandR first
    int primary_screen_width, primary_screen_height;
    int primary_screen_x = 0, primary_screen_y = 0;

    MonitorInfo primary_monitor = get_primary_monitor_xrandr(display, screen);

    if (primary_monitor.found) {
        // Use XRandR info
        primary_screen_width = primary_monitor.width;
        primary_screen_height = primary_monitor.height;
        primary_screen_x = primary_monitor.x;
        primary_screen_y = primary_monitor.y;
        printf("Using XRandR primary monitor info\n");
    } else {
        // Fallback to heuristics
        printf("Falling back to heuristic monitor detection\n");
        const int total_width = DisplayWidth(display, screen);
        const int total_height = DisplayHeight(display, screen);

        // Simple heuristic for multi-monitor detection:
        // If aspect ratio suggests multiple monitors, estimate primary monitor
        // size
        const double aspect_ratio =
            static_cast<double>(total_width) / total_height;

        if (aspect_ratio > 3.0) {
            // Very wide screen, likely dual monitor setup
            primary_screen_width = total_width / 2;
            primary_screen_height = total_height;
        } else if (aspect_ratio > 2.5) {
            // Wide screen, could be ultrawide or dual monitor
            // Use 60% of width as a reasonable estimate for primary monitor
            primary_screen_width = static_cast<int>(total_width * 0.6);
            primary_screen_height = total_height;
        } else {
            // Normal aspect ratio, likely single monitor
            primary_screen_width = total_width;
            primary_screen_height = total_height;
        }
        // No offset information available with heuristics
        primary_screen_x = 0;
        primary_screen_y = 0;
    }

    screen_height = primary_screen_height;

    // Calculate window dimensions based on primary screen size and config
    // ratios
    width = static_cast<int>(primary_screen_width * config.width_ratio);
    const int input_height =
        ui::calculate_actual_input_height(config, primary_screen_height);
    const int item_height =
        ui::calculate_actual_item_height(config, primary_screen_height);
    // Account for: top border + input area + spacing + items + bottom border
    height = static_cast<int>(2 * ui::BORDER_WIDTH + input_height +
                              ui::ITEMS_SPACING +
                              (config.max_visible_items * item_height));

    // Center the window properly: position is relative to center, not top-left
    // corner Also account for monitor offset in multi-monitor setups
    const int x =
        primary_screen_x +
        static_cast<int>(primary_screen_width * config.x_position - width / 2);
    const int y = primary_screen_y +
                  static_cast<int>(primary_screen_height * config.y_position -
                                   height / 2);

    // Debug output
    printf("Primary monitor: %dx%d at (%d,%d)\n", primary_screen_width,
           primary_screen_height, primary_screen_x, primary_screen_y);
    printf("Window: %dx%d at (%d,%d)\n", width, height, x, y);

    XVisualInfo vinfo;
    XMatchVisualInfo(display, screen, 32, TrueColor, &vinfo);

    colormap = XCreateColormap(display, RootWindow(display, screen),
                               vinfo.visual, AllocNone);

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.colormap = colormap;
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                       ButtonPressMask | FocusChangeMask;

    window = XCreateWindow(display, RootWindow(display, screen), x, y, width,
                           height, 0, vinfo.depth, InputOutput, vinfo.visual,
                           CWOverrideRedirect | CWColormap | CWBackPixel |
                               CWBorderPixel | CWEventMask,
                           &attrs);

    Atom windowType = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom dialogType = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, window, windowType, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&dialogType, 1);

    Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom stateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(display, window, stateAtom, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&stateAbove, 1);

    XMapRaised(display, window);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XFlush(display);
}

XWindow::~XWindow()
{
    if (display) {
        if (window)
            XDestroyWindow(display, window);
        if (colormap)
            XFreeColormap(display, colormap);
        XCloseDisplay(display);
    }
}

std::vector<ui::UserInputEvent> get_input_events(Display *display,
                                                 bool blocking)
{
    std::vector<ui::UserInputEvent> events;
    XEvent event;

    // If blocking, wait for at least one event; otherwise only process pending
    // events
    if (blocking) {
        XNextEvent(display, &event);
        // Process the first event
        goto process_event;
    }

    // Then process any additional pending events without blocking
    while (XPending(display) > 0) {
        XNextEvent(display, &event);

    process_event:

        if (event.type == Expose) {
            if (event.xexpose.count == 0) {
                // Expose events don't generate UI events
            }
        } else if (event.type == ButtonPress) {
            // Regain focus when clicked
            XSetInputFocus(display, event.xbutton.window, RevertToParent,
                           CurrentTime);
        } else if (event.type == KeyPress) {
            const KeySym keysym = XLookupKeysym(&event.xkey, 0);

            if (keysym == XK_Escape) {
                events.push_back(ui::KeyboardEvent{.key = ui::KeyCode::Escape,
                                                   .modifier = std::nullopt,
                                                   .character = std::nullopt});
            } else if (keysym == XK_Up) {
                events.push_back(ui::KeyboardEvent{.key = ui::KeyCode::Up,
                                                   .modifier = std::nullopt,
                                                   .character = std::nullopt});
            } else if (keysym == XK_Down) {
                events.push_back(ui::KeyboardEvent{.key = ui::KeyCode::Down,
                                                   .modifier = std::nullopt,
                                                   .character = std::nullopt});
            } else if (keysym == XK_Tab) {
                events.push_back(ui::KeyboardEvent{.key = ui::KeyCode::Tab,
                                                   .modifier = std::nullopt,
                                                   .character = std::nullopt});
            } else if (keysym == XK_Left) {
                events.push_back(ui::KeyboardEvent{.key = ui::KeyCode::Left,
                                                   .modifier = std::nullopt,
                                                   .character = std::nullopt});
            } else if (keysym == XK_Right) {
                events.push_back(ui::KeyboardEvent{.key = ui::KeyCode::Right,
                                                   .modifier = std::nullopt,
                                                   .character = std::nullopt});
            } else if (keysym == XK_Home) {
                events.push_back(ui::KeyboardEvent{.key = ui::KeyCode::Home,
                                                   .modifier = std::nullopt,
                                                   .character = std::nullopt});
            } else if (keysym == XK_End) {
                events.push_back(ui::KeyboardEvent{.key = ui::KeyCode::End,
                                                   .modifier = std::nullopt,
                                                   .character = std::nullopt});
            } else if (keysym == XK_Return) {
                events.push_back(ui::KeyboardEvent{.key = ui::KeyCode::Return,
                                                   .modifier = std::nullopt,
                                                   .character = std::nullopt});
            } else if (keysym == XK_BackSpace) {
                events.push_back(
                    ui::KeyboardEvent{.key = ui::KeyCode::BackSpace,
                                      .modifier = std::nullopt,
                                      .character = std::nullopt});
            } else {
                // Handle regular character input
                std::array<char, 32> char_buffer;
                const int len =
                    XLookupString(&event.xkey, char_buffer.data(),
                                  char_buffer.size(), nullptr, nullptr);
                if (len > 0) {
                    char_buffer[len] = '\0';
                    // Only add printable characters
                    for (int i = 0; i < len; ++i) {
                        if (char_buffer[i] >= 32 && char_buffer[i] < 127) {
                            events.push_back(ui::KeyboardEvent{
                                .key = ui::KeyCode::Character,
                                .modifier = std::nullopt,
                                .character = char_buffer[i],
                            });
                        }
                    }
                }
            }
            break;
        }
    }
    return events;
}
