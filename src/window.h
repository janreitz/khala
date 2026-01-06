#pragma once

#include "types.h"

#include <vector>

#ifdef PLATFORM_X11
    #include <X11/X.h>
    #include <X11/Xatom.h>
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <cairo-xlib.h>
#elif defined(PLATFORM_WAYLAND)
    #include <wayland-client.h>
    #include <xkbcommon/xkbcommon.h>
    #include <cairo.h>

    struct xdg_wm_base;
    struct xdg_surface;
    struct xdg_toplevel;
    struct xdg_activation_v1;
    struct xdg_activation_token_v1;
#elif defined(PLATFORM_WIN32)
    #include <windows.h>
    #include <cairo-win32.h>
#endif

#include <cairo.h>

class PlatformWindow {
public:
    PlatformWindow(ui::RelScreenCoord top_left, ui::RelScreenCoord dimension);
    ~PlatformWindow();

    // Window operations
    void resize(unsigned int height, unsigned int width);
    cairo_surface_t* create_cairo_surface(unsigned int height, unsigned int width);
    std::vector<ui::UserInputEvent> get_input_events(bool blocking = true);

    // Accessors
    int get_width() const { return width; }
    unsigned int get_height() const { return height; }
    unsigned int get_screen_height() const { return screen_height; }

    // Non-copyable
    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

#ifdef PLATFORM_WAYLAND
    // Friend declarations for Wayland listener callbacks
    friend void registry_global_handler(void*, struct wl_registry*, uint32_t, const char*, uint32_t);
    friend void toplevel_configure_handler(void*, struct xdg_toplevel*, int32_t, int32_t, struct wl_array*);
    friend void activation_token_done_handler(void*, struct xdg_activation_token_v1*, const char*);
    friend void keyboard_keymap_handler(void*, struct wl_keyboard*, uint32_t, int, uint32_t);
    friend void keyboard_key_handler(void*, struct wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t);
    friend void keyboard_modifiers_handler(void*, struct wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    friend void pointer_enter_handler(void *, wl_pointer *, uint32_t, wl_surface *, wl_fixed_t, wl_fixed_t);
    friend void seat_capabilities_handler(void*, struct wl_seat*, uint32_t);
#endif

private:
#ifdef PLATFORM_X11
    Display* display;
    ::Window window;
    Colormap colormap;
#elif defined(PLATFORM_WAYLAND)
    wl_display* display;
    wl_compositor* compositor;
    wl_surface* surface;
    wl_seat* seat;
    wl_keyboard* keyboard;
    wl_pointer* pointer;
    xdg_wm_base* xdg_shell;
    xdg_surface* xdg_surface_obj;
    xdg_toplevel* toplevel;
    xdg_activation_v1* activation_protocol;
    xdg_activation_token_v1* activation_token;

    // XKB for keyboard handling
    xkb_context* xkb_ctx;
    xkb_keymap* keymap;
    xkb_state* kb_state;
    
    // XKB for mouse handling
    uint32_t pointer_serial;

    // Shared memory buffer for rendering
    wl_shm* shm;
    wl_buffer* buffer;
    void* shm_data;
    int buffer_fd;

    std::vector<ui::UserInputEvent> pending_events;
#elif defined(PLATFORM_WIN32)
    HWND hwnd;
    HDC hdc;
#endif

    // Common members (all platforms)
    int width;
    unsigned int height;
    unsigned int screen_height;
};
