#ifdef PLATFORM_X11

#include "logger.h"
#include "window.h"

#include "types.h"

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/randr.h>

#include <cairo-xlib.h>
#include <cairo.h>

#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

struct MonitorInfo {
    unsigned int width;
    unsigned int height;
    int x;
    int y;
};

std::optional<MonitorInfo> get_primary_monitor_xrandr(Display *display,
                                                      int screen)
{
    // Check if XRandR extension is available
    int xrandr_event_base, xrandr_error_base;
    if (!XRRQueryExtension(display, &xrandr_event_base, &xrandr_error_base)) {
        LOG_WARNING("XRandR extension not available");
        return std::nullopt;
    }

    // Check XRandR version
    int major_version, minor_version;
    if (!XRRQueryVersion(display, &major_version, &minor_version)) {
        LOG_WARNING("XRandR version query failed");
        return std::nullopt;
    }

    LOG_INFO("XRandR version: %d.%d", major_version, minor_version);

    // We need at least XRandR 1.2 for monitor info
    if (major_version < 1 || (major_version == 1 && minor_version < 2)) {
        LOG_WARNING("XRandR version too old (need 1.2+)");
        return std::nullopt;
    }

    const ::Window root = RootWindow(display, screen);

    // Get screen resources
    XRRScreenResources *screen_resources = XRRGetScreenResources(display, root);
    if (!screen_resources) {
        LOG_ERROR("Failed to get XRandR screen resources");
        return std::nullopt;
    }

    std::optional<MonitorInfo> result;

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
                result = MonitorInfo{crtc_info->width, crtc_info->height,
                                     crtc_info->x, crtc_info->y};
                XRRFreeCrtcInfo(crtc_info);
            }
        }
        if (output_info)
            XRRFreeOutputInfo(output_info);
    }

    XRRFreeScreenResources(screen_resources);
    return result;
}

// Convert X11 modifier state to ui::KeyModifier
ui::KeyModifier x11_to_modifiers(unsigned int state)
{
    ui::KeyModifier mods = ui::KeyModifier::NoModifier;
    if (state & ControlMask)
        mods |= ui::KeyModifier::Ctrl;
    if (state & Mod1Mask)
        mods |= ui::KeyModifier::Alt;
    if (state & ShiftMask)
        mods |= ui::KeyModifier::Shift;
    if (state & Mod4Mask)
        mods |= ui::KeyModifier::Super;
    return mods;
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
    unsigned int primary_screen_width, primary_screen_height;
    int primary_screen_x = 0, primary_screen_y = 0;

    ;

    if (const auto primary_monitor =
            get_primary_monitor_xrandr(display, screen)) {
        // Use XRandR info
        primary_screen_width = primary_monitor->width;
        primary_screen_height = primary_monitor->height;
        primary_screen_x = primary_monitor->x;
        primary_screen_y = primary_monitor->y;
        LOG_INFO("Using XRandR primary monitor info");
    } else {
        // Fallback to heuristics
        LOG_INFO("Falling back to heuristic monitor detection");
        const auto total_width =
            static_cast<unsigned int>(DisplayWidth(display, screen));
        const auto total_height =
            static_cast<unsigned int>(DisplayHeight(display, screen));

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
            primary_screen_width = static_cast<unsigned int>(total_width * 0.6);
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
    width = static_cast<unsigned int>(primary_screen_width * dimension.x);
    height = static_cast<unsigned int>(primary_screen_height * dimension.y);

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
                       ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                       EnterWindowMask | LeaveWindowMask | FocusChangeMask;

    window = XCreateWindow(display, RootWindow(display, screen), x, y, width,
                           height, 0, vinfo.depth, InputOutput, vinfo.visual,
                           CWOverrideRedirect | CWColormap | CWBackPixel |
                               CWBorderPixel | CWEventMask,
                           &attrs);

    const Atom windowType = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom dialogType = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, window, windowType, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&dialogType, 1);

    const Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom stateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(display, window, stateAtom, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&stateAbove, 1);

    XMapRaised(display, window);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XFlush(display);
}

PlatformWindow::~PlatformWindow()
{
    if (cached_context) {
        cairo_destroy(cached_context);
    }
    if (cached_surface) {
        cairo_surface_destroy(cached_surface);
    }
    if (display) {
        if (window)
            XDestroyWindow(display, window);
        if (colormap)
            XFreeColormap(display, colormap);
        XCloseDisplay(display);
    }
}

void PlatformWindow::resize(const ui::WindowDimension &dimension)
{
    XResizeWindow(display, window, dimension.width, dimension.height);
    height = dimension.height;
    width = dimension.width;
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

cairo_t *PlatformWindow::get_cairo_context()
{
    if (surface_cache_valid() && cached_context) {
        return cached_context;
    }

    if (cached_context) {
        cairo_destroy(cached_context);
        cached_context = nullptr;
    }

    // Get the current surface
    cairo_surface_t *surface = get_cairo_surface();

    // Create context if we don't have one
    cached_context = cairo_create(surface);
    if (cached_context == nullptr ||
        cairo_status(cached_context) != CAIRO_STATUS_SUCCESS) {
        const int status =
            cached_context != nullptr ? cairo_status(cached_context) : -1;
        if (cached_context != nullptr) {
            cairo_destroy(cached_context);
            cached_context = nullptr;
        }
        throw std::runtime_error("Failed to create Cairo context, status: " +
                                 std::to_string(status));
    }
    LOG_DEBUG("Created new Cairo context");

    return cached_context;
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

cairo_surface_t *
PlatformWindow::create_cairo_surface(unsigned int surface_height,
                                     unsigned int surface_width)
{
    XWindowAttributes window_attrs;
    XGetWindowAttributes(display, window, &window_attrs);
    Visual *visual = window_attrs.visual;

    // Create Cairo surface for X11 window
    return cairo_xlib_surface_create(display, window, visual,
                                     static_cast<int>(surface_width),
                                     static_cast<int>(surface_height));
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

            // Handle scroll wheel events (Button4 = scroll up, Button5 = scroll
            // down)
            if (event.xbutton.button == Button4) {
                events.push_back(ui::MouseScrollEvent{
                    .direction = ui::MouseScrollEvent::Direction::Up,
                    .position = ui::WindowCoord{.x = event.xbutton.x,
                                                .y = event.xbutton.y}});
                continue;
            } else if (event.xbutton.button == Button5) {
                events.push_back(ui::MouseScrollEvent{
                    .direction = ui::MouseScrollEvent::Direction::Down,
                    .position = ui::WindowCoord{.x = event.xbutton.x,
                                                .y = event.xbutton.y}});
                continue;
            }

            // Generate mouse button event
            ui::MouseButtonEvent::Button button;
            if (event.xbutton.button == Button1) {
                button = ui::MouseButtonEvent::Button::Left;
            } else if (event.xbutton.button == Button2) {
                button = ui::MouseButtonEvent::Button::Middle;
            } else if (event.xbutton.button == Button3) {
                button = ui::MouseButtonEvent::Button::Right;
            } else {
                // Ignore other buttons
                continue;
            }

            events.push_back(ui::MouseButtonEvent{
                .button = button,
                .pressed = true,
                .position = ui::WindowCoord{.x = event.xbutton.x,
                                            .y = event.xbutton.y}});
        } else if (event.type == ButtonRelease) {
            // Generate mouse button release event
            ui::MouseButtonEvent::Button button;
            if (event.xbutton.button == Button1) {
                button = ui::MouseButtonEvent::Button::Left;
            } else if (event.xbutton.button == Button2) {
                button = ui::MouseButtonEvent::Button::Middle;
            } else if (event.xbutton.button == Button3) {
                button = ui::MouseButtonEvent::Button::Right;
            } else {
                // Ignore other buttons (scroll wheel, etc.)
                continue;
            }

            events.push_back(ui::MouseButtonEvent{
                .button = button,
                .pressed = false,
                .position = ui::WindowCoord{.x = event.xbutton.x,
                                            .y = event.xbutton.y}});
        } else if (event.type == MotionNotify) {
            events.push_back(ui::MousePositionEvent{
                .position = ui::WindowCoord{.x = event.xmotion.x,
                                            .y = event.xmotion.y}});
        } else if (event.type == EnterNotify) {
            events.push_back(ui::CursorEnterEvent{
                .position = ui::WindowCoord{.x = event.xcrossing.x,
                                            .y = event.xcrossing.y}});
        } else if (event.type == LeaveNotify) {
            events.push_back(ui::CursorLeaveEvent{});
        } else if (event.type == KeyPress) {
            // Check if this is our registered global hotkey
            if (hotkey_registered) {
                // Mask out NumLock (Mod2Mask) and CapsLock (LockMask) for
                // comparison
                auto clean_state =
                    event.xkey.state &
                    ~static_cast<unsigned int>(Mod2Mask | LockMask);
                if (event.xkey.keycode == hotkey_keycode &&
                    clean_state == hotkey_modifiers) {
                    events.push_back(ui::HotkeyEvent{});
                    continue;
                }
            }

            const KeySym keysym = XLookupKeysym(&event.xkey, 0);
            const ui::KeyModifier modifiers =
                x11_to_modifiers(event.xkey.state);

            ui::KeyCode key = ui::KeyCode::NoKey;

            // Map special keys
            switch (keysym) {
            case XK_Escape:
                key = ui::KeyCode::Escape;
                break;
            case XK_Up:
                key = ui::KeyCode::Up;
                break;
            case XK_Down:
                key = ui::KeyCode::Down;
                break;
            case XK_Tab:
                key = ui::KeyCode::Tab;
                break;
            case XK_Left:
                key = ui::KeyCode::Left;
                break;
            case XK_Right:
                key = ui::KeyCode::Right;
                break;
            case XK_Home:
                key = ui::KeyCode::Home;
                break;
            case XK_End:
                key = ui::KeyCode::End;
                break;
            case XK_Return:
                key = ui::KeyCode::Return;
                break;
            case XK_BackSpace:
                key = ui::KeyCode::BackSpace;
                break;
            case XK_Delete:
                key = ui::KeyCode::Delete;
                break;
            default:
                break;
            }

            if (key != ui::KeyCode::NoKey) {
                events.push_back(ui::KeyboardEvent{.key = key,
                                                   .modifiers = modifiers,
                                                   .character = std::nullopt});
            } else if (keysym >= XK_a && keysym <= XK_z &&
                       ui::has_modifier(modifiers, ui::KeyModifier::Ctrl)) {
                // Handle Ctrl+letter combinations (for hotkeys like Ctrl+Q)
                // XLookupString won't give us the letter when Ctrl is held
                key = static_cast<ui::KeyCode>(
                    static_cast<int>(ui::KeyCode::A) + (keysym - XK_a));
                events.push_back(ui::KeyboardEvent{.key = key,
                                                   .modifiers = modifiers,
                                                   .character = std::nullopt});
            } else if (keysym >= XK_0 && keysym <= XK_9 &&
                       ui::has_modifier(modifiers, ui::KeyModifier::Ctrl)) {
                // Handle Ctrl+number combinations (for hotkeys like Ctrl+1-9)
                // XLookupString won't give us the number when Ctrl is held
                key = static_cast<ui::KeyCode>(
                    static_cast<int>(ui::KeyCode::Num0) + (keysym - XK_0));
                events.push_back(ui::KeyboardEvent{.key = key,
                                                   .modifiers = modifiers,
                                                   .character = std::nullopt});
            } else {
                // Handle regular character input
                constexpr int BUFFER_SIZE = 32;
                std::array<char, BUFFER_SIZE> char_buffer{};
                const auto len = static_cast<size_t>(
                    XLookupString(&event.xkey, char_buffer.data(),
                                  char_buffer.size(), nullptr, nullptr));
                if (len > 0) {
                    // Only add printable characters
                    for (size_t i = 0; i < len; ++i) {
                        if (char_buffer[i] >= 32 && char_buffer[i] < 127) {
                            events.push_back(ui::KeyboardEvent{
                                .key = ui::KeyCode::Character,
                                .modifiers = ui::KeyModifier::NoModifier,
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

void PlatformWindow::commit_surface()
{
    // Get the cairo surface to flush
    cairo_surface_t *cairo_surface = get_cairo_surface();
    if (cairo_surface == nullptr) {
        throw std::runtime_error(
            "Cannot commit surface: no cairo surface available");
    }

    // Flush cairo operations to the underlying buffer
    // For X11, this ensures all drawing operations are completed
    cairo_surface_flush(cairo_surface);
}

// ============================================================================
// PlatformWindow - Visibility Control
// ============================================================================

void PlatformWindow::show()
{
    XMapRaised(display, window);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XFlush(display);
}

void PlatformWindow::hide()
{
    XUnmapWindow(display, window);
    XFlush(display);
}

bool PlatformWindow::is_visible() const
{
    XWindowAttributes attrs;
    if (XGetWindowAttributes(display, window, &attrs)) {
        return attrs.map_state == IsViewable;
    }
    return false;
}

// ============================================================================
// PlatformWindow - Global Hotkey
// ============================================================================

namespace
{

// Convert ui::KeyCode to X11 KeySym
KeySym keycode_to_keysym(ui::KeyCode key)
{
    switch (key) {
    case ui::KeyCode::Escape:
        return XK_Escape;
    case ui::KeyCode::Return:
        return XK_Return;
    case ui::KeyCode::BackSpace:
        return XK_BackSpace;
    case ui::KeyCode::Delete:
        return XK_Delete;
    case ui::KeyCode::Tab:
        return XK_Tab;
    case ui::KeyCode::Space:
        return XK_space;
    case ui::KeyCode::Up:
        return XK_Up;
    case ui::KeyCode::Down:
        return XK_Down;
    case ui::KeyCode::Left:
        return XK_Left;
    case ui::KeyCode::Right:
        return XK_Right;
    case ui::KeyCode::Home:
        return XK_Home;
    case ui::KeyCode::End:
        return XK_End;
    // Letters A-Z
    case ui::KeyCode::A:
        return XK_a;
    case ui::KeyCode::B:
        return XK_b;
    case ui::KeyCode::C:
        return XK_c;
    case ui::KeyCode::D:
        return XK_d;
    case ui::KeyCode::E:
        return XK_e;
    case ui::KeyCode::F:
        return XK_f;
    case ui::KeyCode::G:
        return XK_g;
    case ui::KeyCode::H:
        return XK_h;
    case ui::KeyCode::I:
        return XK_i;
    case ui::KeyCode::J:
        return XK_j;
    case ui::KeyCode::K:
        return XK_k;
    case ui::KeyCode::L:
        return XK_l;
    case ui::KeyCode::M:
        return XK_m;
    case ui::KeyCode::N:
        return XK_n;
    case ui::KeyCode::O:
        return XK_o;
    case ui::KeyCode::P:
        return XK_p;
    case ui::KeyCode::Q:
        return XK_q;
    case ui::KeyCode::R:
        return XK_r;
    case ui::KeyCode::S:
        return XK_s;
    case ui::KeyCode::T:
        return XK_t;
    case ui::KeyCode::U:
        return XK_u;
    case ui::KeyCode::V:
        return XK_v;
    case ui::KeyCode::W:
        return XK_w;
    case ui::KeyCode::X:
        return XK_x;
    case ui::KeyCode::Y:
        return XK_y;
    case ui::KeyCode::Z:
        return XK_z;
    // Numbers 0-9
    case ui::KeyCode::Num0:
        return XK_0;
    case ui::KeyCode::Num1:
        return XK_1;
    case ui::KeyCode::Num2:
        return XK_2;
    case ui::KeyCode::Num3:
        return XK_3;
    case ui::KeyCode::Num4:
        return XK_4;
    case ui::KeyCode::Num5:
        return XK_5;
    case ui::KeyCode::Num6:
        return XK_6;
    case ui::KeyCode::Num7:
        return XK_7;
    case ui::KeyCode::Num8:
        return XK_8;
    case ui::KeyCode::Num9:
        return XK_9;
    // Function keys
    case ui::KeyCode::F1:
        return XK_F1;
    case ui::KeyCode::F2:
        return XK_F2;
    case ui::KeyCode::F3:
        return XK_F3;
    case ui::KeyCode::F4:
        return XK_F4;
    case ui::KeyCode::F5:
        return XK_F5;
    case ui::KeyCode::F6:
        return XK_F6;
    case ui::KeyCode::F7:
        return XK_F7;
    case ui::KeyCode::F8:
        return XK_F8;
    case ui::KeyCode::F9:
        return XK_F9;
    case ui::KeyCode::F10:
        return XK_F10;
    case ui::KeyCode::F11:
        return XK_F11;
    case ui::KeyCode::F12:
        return XK_F12;
    default:
        return NoSymbol;
    }
}

// Convert ui::KeyModifier to X11 modifier mask
unsigned int modifiers_to_x11(ui::KeyModifier mods)
{
    unsigned int x11_mods = 0;
    if (ui::has_modifier(mods, ui::KeyModifier::Ctrl))
        x11_mods |= ControlMask;
    if (ui::has_modifier(mods, ui::KeyModifier::Alt))
        x11_mods |= Mod1Mask;
    if (ui::has_modifier(mods, ui::KeyModifier::Shift))
        x11_mods |= ShiftMask;
    if (ui::has_modifier(mods, ui::KeyModifier::Super))
        x11_mods |= Mod4Mask;
    return x11_mods;
}

// X11 error handler for catching grab errors
static bool g_grab_error_occurred = false;
static int grab_error_handler(Display * /*display*/, XErrorEvent *event)
{
    if (event->error_code == BadAccess) {
        g_grab_error_occurred = true;
    }
    return 0;
}

} // anonymous namespace

bool PlatformWindow::register_global_hotkey(const ui::KeyboardEvent &hotkey)
{
    // Unregister any existing hotkey first
    if (hotkey_registered) {
        unregister_global_hotkey();
    }

    const KeySym keysym = keycode_to_keysym(hotkey.key);
    if (keysym == NoSymbol) {
        LOG_ERROR("Cannot convert key to X11 KeySym");
        return false;
    }

    const ::KeyCode keycode = XKeysymToKeycode(display, keysym);
    if (keycode == 0) {
        LOG_ERROR("Cannot convert KeySym to X11 KeyCode");
        return false;
    }

    const unsigned int x11_mods = modifiers_to_x11(hotkey.modifiers);
    const ::Window root = DefaultRootWindow(display);

    // Install error handler to catch BadAccess
    g_grab_error_occurred = false;
    auto old_handler = XSetErrorHandler(grab_error_handler);

    // Grab the key on the root window
    XGrabKey(display, keycode, x11_mods, root, True, GrabModeAsync,
             GrabModeAsync);

    // Sync to catch any errors from the primary grab
    XSync(display, False);

    if (g_grab_error_occurred) {
        XSetErrorHandler(old_handler);
        LOG_ERROR("XGrabKey failed - hotkey may already be in use by another "
                  "application");
        return false;
    }

    // Also try to grab with NumLock (Mod2Mask) and CapsLock (LockMask) to
    // handle those states These are best-effort - if they fail (e.g., already
    // grabbed), we continue anyway
    g_grab_error_occurred = false;
    XGrabKey(display, keycode, x11_mods | Mod2Mask, root, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(display, keycode, x11_mods | LockMask, root, True, GrabModeAsync,
             GrabModeAsync);
    XGrabKey(display, keycode, x11_mods | Mod2Mask | LockMask, root, True,
             GrabModeAsync, GrabModeAsync);

    // Sync and ignore errors from the extra modifier grabs
    XSync(display, False);

    // Restore original error handler
    XSetErrorHandler(old_handler);

    hotkey_registered = true;
    hotkey_keycode = keycode;
    hotkey_modifiers = x11_mods;

    LOG_INFO("Registered global hotkey (keycode=%d, mods=0x%x)", keycode,
             x11_mods);
    return true;
}

void PlatformWindow::unregister_global_hotkey()
{
    if (!hotkey_registered) {
        return;
    }

    const ::Window root = DefaultRootWindow(display);

    // Ungrab all the modifier combinations we grabbed
    XUngrabKey(display, hotkey_keycode, hotkey_modifiers, root);
    XUngrabKey(display, hotkey_keycode, hotkey_modifiers | Mod2Mask, root);
    XUngrabKey(display, hotkey_keycode, hotkey_modifiers | LockMask, root);
    XUngrabKey(display, hotkey_keycode, hotkey_modifiers | Mod2Mask | LockMask,
               root);

    XFlush(display);

    hotkey_registered = false;
    hotkey_keycode = 0;
    hotkey_modifiers = 0;

    LOG_INFO("Unregistered global hotkey");
}

#endif // PLATFORM_X11
