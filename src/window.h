#pragma once

#include "types.h"

#include <map>
#include <vector>

#ifdef PLATFORM_X11
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo-xlib.h>
#elif defined(PLATFORM_WAYLAND)
#include <cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_activation_v1;
struct xdg_activation_token_v1;

// Buffer pool entry for Wayland buffer lifecycle management
struct WaylandBuffer {
    wl_buffer* wl_buffer_obj;
    void* shm_data;
    int shm_fd;
    cairo_surface_t* cairo_surface;

    int width;
    int height;
    size_t size;

    enum class State {
        FREE,        // Available for reuse or destruction
        DRAWING,     // Currently being drawn to by Cairo
        SUBMITTED,   // Attached to surface, waiting for compositor release
    } state;

    uint64_t last_used_frame;
};
#elif defined(PLATFORM_WIN32)
#include <cairo-win32.h>
#include <windows.h>
#endif

#include <cairo.h>

class PlatformWindow
{
  public:
    PlatformWindow(ui::RelScreenCoord top_left, ui::RelScreenCoord dimension);
    ~PlatformWindow();

    // Window operations
    void resize(unsigned int height, unsigned int width);
    // Returns cairo context for drawing
    // The context and underlying surface are owned by PlatformWindow
    // Caller must not destroy the returned context
    cairo_t* get_cairo_context();
    // Commits the rendered surface to display
    // Includes cairo_surface_flush() and platform-specific commit (e.g., wl_surface_commit)
    void commit_surface();
    std::vector<ui::UserInputEvent> get_input_events(bool blocking = true);

    // Accessors
    int get_width() const { return width; }
    unsigned int get_height() const { return height; }
    unsigned int get_screen_height() const { return screen_height; }

    // Non-copyable
    PlatformWindow(const PlatformWindow &) = delete;
    PlatformWindow &operator=(const PlatformWindow &) = delete;

#ifdef PLATFORM_WAYLAND
    // Friend declarations for Wayland listener callbacks
    friend void registry_global_handler(void *, struct wl_registry *, uint32_t,
                                        const char *, uint32_t);
    friend void toplevel_configure_handler(void *, struct xdg_toplevel *,
                                           int32_t, int32_t, struct wl_array *);
    friend void activation_token_done_handler(void *,
                                              struct xdg_activation_token_v1 *,
                                              const char *);
    friend void keyboard_keymap_handler(void *, struct wl_keyboard *, uint32_t,
                                        int, uint32_t);
    friend void keyboard_key_handler(void *, struct wl_keyboard *, uint32_t,
                                     uint32_t, uint32_t, uint32_t);
    friend void keyboard_modifiers_handler(void *, struct wl_keyboard *,
                                           uint32_t, uint32_t, uint32_t,
                                           uint32_t, uint32_t);
    friend void pointer_enter_handler(void *, wl_pointer *, uint32_t,
                                      wl_surface *, wl_fixed_t, wl_fixed_t);
    friend void seat_capabilities_handler(void *, struct wl_seat *, uint32_t);
    friend void output_geometry_handler(void *, struct wl_output *, int32_t,
                                        int32_t, int32_t, int32_t, int32_t,
                                        const char *, const char *, int32_t);
    friend void output_mode_handler(void *, struct wl_output *, uint32_t,
                                    int32_t, int32_t, int32_t);
    friend void output_scale_handler(void *data, wl_output *output,
                                     int32_t factor);
    friend void output_name_handler(void *data, wl_output *output,
                                    const char *name);
    friend void output_description_handler(void *data, wl_output *output,
                                           const char *description);
    friend void output_done_handler(void *data, wl_output *output);
#endif

  private:
#ifdef PLATFORM_X11
    cairo_surface_t *create_cairo_surface(int h, int w);
    cairo_surface_t *get_cairo_surface();
    bool surface_cache_valid() const;

    cairo_surface_t *cached_surface = nullptr;
    cairo_t *cached_context = nullptr;
    int cached_surface_width = 0;
    int cached_surface_height = 0;
    Display *display;
    ::Window window;
    Colormap colormap;
#elif defined(PLATFORM_WAYLAND)
    cairo_surface_t *create_cairo_surface(int h, int w);
    cairo_surface_t *get_cairo_surface();
    bool surface_cache_valid() const;

    wl_display *display;
    wl_compositor *compositor;
    wl_surface *surface;
    wl_seat *seat;
    wl_keyboard *keyboard;
    wl_pointer *pointer;
    xdg_wm_base *xdg_shell;
    xdg_surface *xdg_surface_obj;
    xdg_toplevel *toplevel;
    xdg_activation_v1 *activation_protocol;
    xdg_activation_token_v1 *activation_token;

    struct OutputInfo {
        std::string name;
        std::string description;
        std::string make;
        std::string model;
        int32_t x;
        int32_t y;
        int32_t width;
        int32_t height;
        int32_t scale;
        int32_t refresh;
        bool done;
    };
    std::map<wl_output *, OutputInfo> output_infos;

    // XKB for keyboard handling
    xkb_context *xkb_ctx;
    xkb_keymap *keymap;
    xkb_state *kb_state;

    // XKB for mouse handling
    uint32_t pointer_serial;

    // Shared memory buffer for rendering
    cairo_surface_t *cached_surface = nullptr;
    cairo_t *cached_context = nullptr;
    int cached_surface_width = 0;
    int cached_surface_height = 0;
    wl_shm *shm;

    // Buffer pool for proper Wayland buffer lifecycle management
    static constexpr size_t MAX_BUFFERS = 3;
    std::vector<WaylandBuffer> buffer_pool;
    WaylandBuffer* current_buffer;
    uint64_t frame_counter;

    // Buffer pool management methods
    WaylandBuffer* allocate_buffer(int width, int height);
    WaylandBuffer* find_free_buffer(int width, int height);
    void create_wl_buffer(WaylandBuffer& buf, int w, int h);
    void cleanup_old_buffers();
    void destroy_buffer(WaylandBuffer& buf);

    std::vector<ui::UserInputEvent> pending_events;
#elif defined(PLATFORM_WIN32)
    HWND hwnd;
    HDC hdc;
#endif

    // Common members (all platforms)
    int width;
    int height;
    unsigned int screen_width;
    unsigned int screen_height;
};
