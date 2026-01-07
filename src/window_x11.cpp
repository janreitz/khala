#ifdef PLATFORM_X11

#include "logger.h"
#include "ui.h"
#include "utility.h"
#include "window.h"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <cairo-xlib.h>
#include <cairo.h>

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
        LOG_WARNING("XRandR extension not available");
        return info;
    }

    // Check XRandR version
    int major_version, minor_version;
    if (!XRRQueryVersion(display, &major_version, &minor_version)) {
        LOG_WARNING("XRandR version query failed");
        return info;
    }

    LOG_INFO("XRandR version: %d.%d", major_version, minor_version);

    // We need at least XRandR 1.2 for monitor info
    if (major_version < 1 || (major_version == 1 && minor_version < 2)) {
        LOG_WARNING("XRandR version too old (need 1.2+)");
        return info;
    }

    ::Window root = RootWindow(display, screen);

    // Get screen resources
    XRRScreenResources *screen_resources = XRRGetScreenResources(display, root);
    if (!screen_resources) {
        LOG_ERROR("Failed to get XRandR screen resources");
        return info;
    }

    // Find primary output
    RROutput primary = XRRGetOutputPrimary(display, root);

    // If no primary is set, use the first connected output
    if (primary == None) {
        LOG_INFO("No primary output set, looking for first connected output");
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

PlatformWindow::PlatformWindow(ui::RelScreenCoord top_left,
                               ui::RelScreenCoord dimension)
{
    display = XOpenDisplay(nullptr);
    if (!display) {
        throw std::runtime_error("Cannot open display");
    }

    const int screen = DefaultScreen(display);

    // Get primary monitor info using XRandR first
    int primary_screen_width, primary_screen_height;
    int primary_screen_x = 0, primary_screen_y = 0;

    const MonitorInfo primary_monitor =
        get_primary_monitor_xrandr(display, screen);

    if (primary_monitor.found) {
        // Use XRandR info
        primary_screen_width = primary_monitor.width;
        primary_screen_height = primary_monitor.height;
        primary_screen_x = primary_monitor.x;
        primary_screen_y = primary_monitor.y;
        LOG_INFO("Using XRandR primary monitor info");
    } else {
        // Fallback to heuristics
        LOG_INFO("Falling back to heuristic monitor detection");
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
    width = static_cast<int>(primary_screen_width * dimension.x);
    height = static_cast<int>(primary_screen_height * dimension.y);

    const int x =
        primary_screen_x + static_cast<int>(primary_screen_width * top_left.x);
    const int y =
        primary_screen_y + static_cast<int>(primary_screen_height * top_left.y);

    // Debug output
    LOG_DEBUG("Primary monitor: %dx%d at (%d,%d)", primary_screen_width,
              primary_screen_height, primary_screen_x, primary_screen_y);
    LOG_DEBUG("Window: %dx%d at (%d,%d)", width, height, x, y);

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

PlatformWindow::~PlatformWindow()
{
    if (display) {
        if (window)
            XDestroyWindow(display, window);
        if (colormap)
            XFreeColormap(display, colormap);
        XCloseDisplay(display);
    }
    if (cached_surface) {
        cairo_surface_destroy(cached_surface);
    }
}

void PlatformWindow::resize(unsigned int new_height, unsigned int new_width)
{
    XResizeWindow(display, window, new_width, new_height);
    height = new_height;
    width = new_width;
}

cairo_surface_t *PlatformWindow::get_cairo_surface()
{
    if (surface_cache_valid()) {
        return cached_surface;
    }

    if (cached_surface != nullptr) {
        cairo_surface_destroy(cached_surface);
        cached_surface = nullptr;
    }

    cached_surface = create_cairo_surface(height, width);
    cached_surface_width = width;
    cached_surface_height = height;
    return cached_surface;
}

bool PlatformWindow::surface_cache_valid() const
{
    if (cached_surface == nullptr) {
        LOG_DEBUG("Surface cache miss: cached_surface == nullptr");
        return false;
    }

    if (cached_surface_width != width || cached_surface_height != height) {
        LOG_DEBUG("Surface cache miss: dimensions changed (cached: %ux%u "
                  "window: %ux%u)",
                  cached_surface_width, cached_surface_height, width, height);
        return false;
    }

    const auto surface_status = cairo_surface_status(cached_surface);
    if (surface_status != CAIRO_STATUS_SUCCESS) {
        LOG_DEBUG("Surface cache miss: cairo_surface_status %d",
                  surface_status);
        return false;
    }
    return true;
}

cairo_surface_t *PlatformWindow::create_cairo_surface(int height,
                                                      int width)
{
    XWindowAttributes window_attrs;
    XGetWindowAttributes(display, window, &window_attrs);
    Visual *visual = window_attrs.visual;

    // Create Cairo surface for X11 window
    return cairo_xlib_surface_create(display, window, visual, width, height);
}

std::vector<ui::UserInputEvent> PlatformWindow::get_input_events(bool blocking)
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

#endif // PLATFORM_X11
