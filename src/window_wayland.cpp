#ifdef PLATFORM_WAYLAND

#include "logger.h"
#include "types.h"
#include "window.h"

#include <cairo.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <xdg-activation-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

namespace
{

// Helper function to create anonymous file for shared memory
int create_anonymous_file(size_t size)
{
    const char *path = getenv("XDG_RUNTIME_DIR");
    if (!path) {
        throw std::runtime_error("XDG_RUNTIME_DIR not set");
    }

    std::string name = std::string(path) + "/khala-shm-XXXXXX";
    int fd = mkstemp(&name[0]);
    if (fd < 0) {
        throw std::runtime_error("Failed to create temporary file");
    }

    unlink(name.c_str());

    if (ftruncate(fd, size) < 0) {
        close(fd);
        throw std::runtime_error("Failed to truncate file");
    }

    return fd;
}

} // anonymous namespace

// Global listeners for Wayland protocols

// Registry listener - binds global Wayland objects
void registry_global_handler(void *data, wl_registry *registry, uint32_t name,
                             const char *interface, uint32_t)
{
    PlatformWindow *win = static_cast<PlatformWindow *>(data);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        win->compositor = static_cast<wl_compositor *>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        win->xdg_shell = static_cast<xdg_wm_base *>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        win->seat = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        win->shm = static_cast<wl_shm *>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, xdg_activation_v1_interface.name) == 0) {
        win->activation_protocol = static_cast<xdg_activation_v1 *>(
            wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1));
    }
}

static void registry_global_remove_handler(void *, wl_registry *, uint32_t)
{
    // Not needed for now
}

static const wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler};

// XDG WM Base listener - handles ping events
static void xdg_wm_base_ping_handler(void *, xdg_wm_base *xdg_shell,
                                     uint32_t serial)
{
    xdg_wm_base_pong(xdg_shell, serial);
}

static const xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping_handler};

// XDG Surface listener - handles configure events
static void xdg_surface_configure_handler(void *, xdg_surface *xdg_surface,
                                          uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure_handler};

// XDG Toplevel listener - handles window state changes
void toplevel_configure_handler(void *data, xdg_toplevel *, int32_t width,
                                int32_t height, wl_array *)
{
    auto win = static_cast<PlatformWindow *>(data);
    if (width > 0 && height > 0) {
        win->resize(height, width);
    }
}

static void toplevel_close_handler(void *, xdg_toplevel *)
{
    // Window close requested - could add exit event
}

static void toplevel_configure_bounds_handler(void *data, xdg_toplevel *,
                                              int32_t width, int32_t height)
{
    auto win = static_cast<PlatformWindow *>(data);
    if (width > 0 && height > 0) {
        win->resize(height, width);
    }
}

void toplevel_wm_capabilities_handler(void *, xdg_toplevel *,

                                      wl_array *)
{
    // TODO
}

static const xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure_handler,
    .close = toplevel_close_handler,
    .configure_bounds = toplevel_configure_bounds_handler,
    .wm_capabilities = toplevel_wm_capabilities_handler,
};

// XDG Activation token listener
void activation_token_done_handler(void *data, xdg_activation_token_v1 *token,
                                   const char *token_str)
{
    PlatformWindow *win = static_cast<PlatformWindow *>(data);

    // Use token to request focus
    if (win->activation_protocol && win->surface) {
        xdg_activation_v1_activate(win->activation_protocol, token_str,
                                   win->surface);
    }

    xdg_activation_token_v1_destroy(token);
    win->activation_token = nullptr;
}

static const xdg_activation_token_v1_listener activation_token_listener = {
    .done = activation_token_done_handler};

// Keyboard listener - handles keyboard events
void keyboard_keymap_handler(void *data, wl_keyboard *, uint32_t format, int fd,
                             uint32_t size)
{
    PlatformWindow *win = static_cast<PlatformWindow *>(data);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map_shm =
        static_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (map_shm == MAP_FAILED) {
        close(fd);
        return;
    }

    xkb_keymap *new_keymap = xkb_keymap_new_from_string(
        win->xkb_ctx, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_shm, size);
    close(fd);

    if (!new_keymap) {
        return;
    }

    xkb_state *new_state = xkb_state_new(new_keymap);
    if (!new_state) {
        xkb_keymap_unref(new_keymap);
        return;
    }

    if (win->kb_state) {
        xkb_state_unref(win->kb_state);
    }
    if (win->keymap) {
        xkb_keymap_unref(win->keymap);
    }

    win->keymap = new_keymap;
    win->kb_state = new_state;
}

static void keyboard_enter_handler(void *, wl_keyboard *, uint32_t,
                                   wl_surface *, wl_array *)
{
    // Keyboard focus gained
}

static void keyboard_leave_handler(void *, wl_keyboard *, uint32_t,
                                   wl_surface *)
{
    // Keyboard focus lost
}

void keyboard_key_handler(void *data, wl_keyboard *, uint32_t, uint32_t,
                          uint32_t key, uint32_t state)
{
    PlatformWindow *win = static_cast<PlatformWindow *>(data);

    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        return; // Only handle key press
    }

    if (!win->kb_state) {
        return;
    }

    // XKB uses key + 8 for keycodes
    xkb_keycode_t keycode = key + 8;
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(win->kb_state, keycode);

    // Map XKB keysyms to our KeyCode enum
    ui::KeyboardEvent event;
    event.modifier = std::nullopt;
    event.character = std::nullopt;

    switch (keysym) {
    case XKB_KEY_Escape:
        event.key = ui::KeyCode::Escape;
        win->pending_events.push_back(event);
        break;
    case XKB_KEY_Return:
        event.key = ui::KeyCode::Return;
        win->pending_events.push_back(event);
        break;
    case XKB_KEY_BackSpace:
        event.key = ui::KeyCode::BackSpace;
        win->pending_events.push_back(event);
        break;
    case XKB_KEY_Tab:
        event.key = ui::KeyCode::Tab;
        win->pending_events.push_back(event);
        break;
    case XKB_KEY_Up:
        event.key = ui::KeyCode::Up;
        win->pending_events.push_back(event);
        break;
    case XKB_KEY_Down:
        event.key = ui::KeyCode::Down;
        win->pending_events.push_back(event);
        break;
    case XKB_KEY_Left:
        event.key = ui::KeyCode::Left;
        win->pending_events.push_back(event);
        break;
    case XKB_KEY_Right:
        event.key = ui::KeyCode::Right;
        win->pending_events.push_back(event);
        break;
    case XKB_KEY_Home:
        event.key = ui::KeyCode::Home;
        win->pending_events.push_back(event);
        break;
    case XKB_KEY_End:
        event.key = ui::KeyCode::End;
        win->pending_events.push_back(event);
        break;
    default:
        // Handle regular character input
        char buffer[8];
        int size = xkb_state_key_get_utf8(win->kb_state, keycode, buffer,
                                          sizeof(buffer));
        if (size > 0 && size < static_cast<int>(sizeof(buffer))) {
            buffer[size] = '\0';
            // Only add printable ASCII characters
            if (buffer[0] >= 32 && buffer[0] < 127 && buffer[1] == '\0') {
                event.key = ui::KeyCode::Character;
                event.character = buffer[0];
                win->pending_events.push_back(event);
            }
        }
        break;
    }
}

void keyboard_modifiers_handler(void *data, wl_keyboard *, uint32_t,
                                uint32_t mods_depressed, uint32_t mods_latched,
                                uint32_t mods_locked, uint32_t group)
{
    PlatformWindow *win = static_cast<PlatformWindow *>(data);
    if (win->kb_state) {
        xkb_state_update_mask(win->kb_state, mods_depressed, mods_latched,
                              mods_locked, 0, 0, group);
    }
}

static void keyboard_repeat_info_handler(void *, wl_keyboard *, int32_t,
                                         int32_t)
{
    // Key repeat info - not needed for now
}

static const wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap_handler,
    .enter = keyboard_enter_handler,
    .leave = keyboard_leave_handler,
    .key = keyboard_key_handler,
    .modifiers = keyboard_modifiers_handler,
    .repeat_info = keyboard_repeat_info_handler};

// Seat listener - handles input device capabilities
void seat_capabilities_handler(void *data, wl_seat *seat, uint32_t caps)
{
    PlatformWindow *win = static_cast<PlatformWindow *>(data);

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !win->keyboard) {
        win->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(win->keyboard, &keyboard_listener, win);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && win->keyboard) {
        wl_keyboard_destroy(win->keyboard);
        win->keyboard = nullptr;
    }

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !win->pointer) {
        win->pointer = wl_seat_get_pointer(seat);
        // Add pointer listener if needed for mouse support
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && win->pointer) {
        wl_pointer_destroy(win->pointer);
        win->pointer = nullptr;
    }
}

static void seat_name_handler(void *, wl_seat *, const char *)
{
    // Seat name - not needed
}

static const wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities_handler, .name = seat_name_handler};

// PlatformWindow implementation

PlatformWindow::PlatformWindow(ui::RelScreenCoord top_left,
                               ui::RelScreenCoord dimension)
    : display(nullptr), compositor(nullptr), surface(nullptr), seat(nullptr),
      keyboard(nullptr), pointer(nullptr), xdg_shell(nullptr),
      xdg_surface_obj(nullptr), toplevel(nullptr), activation_protocol(nullptr),
      activation_token(nullptr), xkb_ctx(nullptr), keymap(nullptr),
      kb_state(nullptr), shm(nullptr), buffer(nullptr), shm_data(nullptr),
      buffer_fd(-1), width(0), height(0), screen_height(1080)
{
    // Connect to Wayland display
    display = wl_display_connect(nullptr);
    if (!display) {
        throw std::runtime_error("Failed to connect to Wayland display");
    }

    // Initialize XKB context
    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx) {
        wl_display_disconnect(display);
        throw std::runtime_error("Failed to create XKB context");
    }

    // Bind Wayland protocols via registry
    wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip(display); // Wait for all globals

    if (!compositor || !xdg_shell || !shm) {
        throw std::runtime_error(
            "Compositor doesn't support required protocols");
    }

    // Add XDG shell listener
    xdg_wm_base_add_listener(xdg_shell, &xdg_wm_base_listener, this);

    // Add seat listener for input
    if (seat) {
        wl_seat_add_listener(seat, &seat_listener, this);
    }

    // TODO: Multi-monitor detection using wl_output protocol
    // For now, use defaults or estimate from config
    screen_height = 1080;                         // Default height
    width = static_cast<int>(1920 * dimension.x); // Default width assumption
    height = static_cast<unsigned int>(screen_height * dimension.y);

    LOG_INFO("Wayland window: %dx%d", width, height);

    // Create surface
    surface = wl_compositor_create_surface(compositor);
    if (!surface) {
        throw std::runtime_error("Failed to create Wayland surface");
    }

    // Create XDG surface and toplevel
    xdg_surface_obj = xdg_wm_base_get_xdg_surface(xdg_shell, surface);
    xdg_surface_add_listener(xdg_surface_obj, &xdg_surface_listener, this);

    toplevel = xdg_surface_get_toplevel(xdg_surface_obj);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, this);

    // Set window properties
    xdg_toplevel_set_title(toplevel, "khala");
    xdg_toplevel_set_app_id(toplevel, "com.khala.launcher");

    // Commit surface to trigger configure
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    // Request focus using XDG Activation
    const char *env_token = getenv("XDG_ACTIVATION_TOKEN");
    if (activation_protocol) {
        if (env_token && env_token[0] != '\0') {
            // Use provided token from environment
            xdg_activation_v1_activate(activation_protocol, env_token, surface);
            unsetenv("XDG_ACTIVATION_TOKEN");
            LOG_INFO("Using XDG activation token from environment");
        } else {
            // Request new token
            activation_token =
                xdg_activation_v1_get_activation_token(activation_protocol);
            xdg_activation_token_v1_add_listener(
                activation_token, &activation_token_listener, this);
            xdg_activation_token_v1_set_surface(activation_token, surface);
            xdg_activation_token_v1_commit(activation_token);
            LOG_INFO("Requesting XDG activation token");
        }
    } else {
        LOG_WARNING(
            "XDG Activation protocol not available - focus may not work");
    }

    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
}

PlatformWindow::~PlatformWindow()
{
    // Cleanup in reverse order of creation
    if (buffer) {
        wl_buffer_destroy(buffer);
    }
    if (shm_data) {
        munmap(shm_data, width * height * 4);
    }
    if (buffer_fd >= 0) {
        close(buffer_fd);
    }

    if (keyboard) {
        wl_keyboard_destroy(keyboard);
    }
    if (pointer) {
        wl_pointer_destroy(pointer);
    }

    if (kb_state) {
        xkb_state_unref(kb_state);
    }
    if (keymap) {
        xkb_keymap_unref(keymap);
    }
    if (xkb_ctx) {
        xkb_context_unref(xkb_ctx);
    }

    if (toplevel) {
        xdg_toplevel_destroy(toplevel);
    }
    if (xdg_surface_obj) {
        xdg_surface_destroy(xdg_surface_obj);
    }
    if (surface) {
        wl_surface_destroy(surface);
    }

    if (seat) {
        wl_seat_destroy(seat);
    }
    if (activation_protocol) {
        xdg_activation_v1_destroy(activation_protocol);
    }
    if (xdg_shell) {
        xdg_wm_base_destroy(xdg_shell);
    }
    if (shm) {
        wl_shm_destroy(shm);
    }
    if (compositor) {
        wl_compositor_destroy(compositor);
    }

    if (display) {
        wl_display_disconnect(display);
    }
}

void PlatformWindow::resize(unsigned int new_height, unsigned int new_width)
{
    height = new_height;
    width = new_width;
    // Note: Wayland clients don't resize directly - the compositor handles it
    // We just track the size for our buffer allocation
}

cairo_surface_t *PlatformWindow::create_cairo_surface(unsigned int h,
                                                      unsigned int w)
{
    LOG_DEBUG("Creating Cairo surface: %ux%u", w, h);
    const size_t stride = w * 4; // 4 bytes per pixel (ARGB32)
    const size_t buffer_size = stride * h;

    // Update window dimensions to match requested size
    width = static_cast<int>(w);
    height = h;

    // Cleanup old buffer if dimensions changed
    if (buffer) {
        wl_buffer_destroy(buffer);
        buffer = nullptr;
        if (shm_data) {
            munmap(shm_data, static_cast<unsigned int>(width) * height * 4);
            shm_data = nullptr;
        }
        if (buffer_fd >= 0) {
            close(buffer_fd);
            buffer_fd = -1;
        }
    }

    // Create new buffer
    buffer_fd = create_anonymous_file(buffer_size);
    if (buffer_fd < 0) {
        throw std::runtime_error("Failed to create anonymous file");
    }

    shm_data = mmap(nullptr, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    buffer_fd, 0);
    if (shm_data == MAP_FAILED) {
        close(buffer_fd);
        buffer_fd = -1;
        throw std::runtime_error("Failed to mmap buffer");
    }

    wl_shm_pool *pool = wl_shm_create_pool(shm, buffer_fd, buffer_size);
    if (!pool) {
        munmap(shm_data, buffer_size);
        shm_data = nullptr;
        close(buffer_fd);
        buffer_fd = -1;
        throw std::runtime_error("Failed to create SHM pool");
    }

    buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                       WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    if (!buffer) {
        munmap(shm_data, buffer_size);
        shm_data = nullptr;
        close(buffer_fd);
        buffer_fd = -1;
        throw std::runtime_error("Failed to create buffer");
    }

    // Create Cairo image surface from our SHM buffer
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        static_cast<unsigned char *>(shm_data), CAIRO_FORMAT_ARGB32, w, h,
        stride);

    // Check if Cairo surface creation was successful
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        LOG_ERROR("Cairo surface creation failed with status: %d",
                  cairo_surface_status(surface));
        cairo_surface_destroy(surface);
        throw std::runtime_error("Failed to create Cairo surface");
    }

    LOG_DEBUG("Successfully created Cairo surface");
    return surface;
}

std::vector<ui::UserInputEvent> PlatformWindow::get_input_events(bool blocking)
{
    if (blocking) {
        wl_display_dispatch(display);
    } else {
        wl_display_dispatch_pending(display);
    }

    // After dispatching, we need to damage and attach the buffer if we have one
    // This ensures our rendered content is displayed
    if (buffer && surface) {
        wl_surface_attach(surface, buffer, 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, width, height);
        wl_surface_commit(surface);
    }

    // Return events collected by listeners
    std::vector<ui::UserInputEvent> events = std::move(pending_events);
    pending_events.clear();
    return events;
}

#endif // PLATFORM_WAYLAND
