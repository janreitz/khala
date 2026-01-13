#ifdef PLATFORM_WAYLAND

#include "logger.h"
#include "types.h"
#include "window.h"

#include <cairo.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <xdg-activation-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
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
    const int fd = mkstemp(&name[0]);
    if (fd < 0) {
        throw std::runtime_error("Failed to create temporary file");
    }

    unlink(name.c_str());

    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        throw std::runtime_error("Failed to truncate file");
    }

    return fd;
}

} // anonymous namespace

// Global listeners for Wayland protocols

// Output listener - handles screen resolution and geometry
void output_geometry_handler(void *data, wl_output *output, int32_t x,
                             int32_t y, int32_t, int32_t, int32_t,
                             const char *make, const char *model, int32_t)
{
    auto *window = static_cast<PlatformWindow *>(data);
    auto &info = window->output_infos[output];
    info.x = x;
    info.y = y;
    info.make = make;
    info.model = model;
}

void output_mode_handler(void *data, wl_output *output, uint32_t flags,
                         int32_t width, int32_t height, int32_t refresh)
{
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        auto *window = static_cast<PlatformWindow *>(data);
        auto &info = window->output_infos[output];
        info.width = width;
        info.height = height;
        info.refresh = refresh;
    }
}

void output_scale_handler(void *data, wl_output *output, int32_t factor)
{
    auto *window = static_cast<PlatformWindow *>(data);
    auto &info = window->output_infos[output];
    info.scale = factor;
}

void output_name_handler(void *data, wl_output *output, const char *name)
{
    auto *window = static_cast<PlatformWindow *>(data);
    auto &info = window->output_infos[output];
    info.name = name;
}

void output_description_handler(void *data, wl_output *output,
                                const char *description)
{
    auto *window = static_cast<PlatformWindow *>(data);
    auto &info = window->output_infos[output];
    info.description = description;
}

void output_done_handler(void *data, wl_output *output)
{
    auto *window = static_cast<PlatformWindow *>(data);
    auto &info = window->output_infos[output];
    info.done = true;
    LOG_DEBUG("Output: name=%s desc=%s make=%s model=%s pos=(%d,%d) "
              "size=%dx%d scale=%d refresh=%d",
              info.name.c_str(), info.description.c_str(), info.make.c_str(),
              info.model.c_str(), info.x, info.y, info.width, info.height,
              info.scale, info.refresh);
}

static const wl_output_listener output_listener = {
    .geometry = output_geometry_handler,
    .mode = output_mode_handler,
    .done = output_done_handler,
    .scale = output_scale_handler,
    .name = output_name_handler,
    .description = output_description_handler};

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
    } else if (strcmp(interface, "wl_output") == 0) {
        auto *output = static_cast<wl_output *>(
            wl_registry_bind(registry, name, &wl_output_interface, 2));

        // Insert with defaults, callbacks will populate
        win->output_infos[output] = PlatformWindow::OutputInfo();

        wl_output_add_listener(output, &output_listener, win);
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
    LOG_DEBUG("Received ping");
    xdg_wm_base_pong(xdg_shell, serial);
}

static const xdg_wm_base_listener my_xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping_handler};

// XDG Surface listener - handles configure events
static void xdg_surface_configure_handler(void *, xdg_surface *xdg_surface,
                                          uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const xdg_surface_listener my_xdg_surface_listener = {
    .configure = xdg_surface_configure_handler};

// XDG Toplevel listener - handles window state changes
void toplevel_configure_handler(void *data, xdg_toplevel *, int32_t width,
                                int32_t height, wl_array *)
{
    auto* win = static_cast<PlatformWindow *>(data);
    if (width > 0 && height > 0) {
        win->resize(ui::WindowDimension{
            .height = static_cast<unsigned int>(height),
            .width = static_cast<unsigned int>(width),
        });
    }
}

static void toplevel_close_handler(void *, xdg_toplevel *)
{
    // Window close requested - could add exit event
}

static void toplevel_configure_bounds_handler(void *data, xdg_toplevel *,
                                              int32_t width, int32_t height)
{
    auto* win = static_cast<PlatformWindow *>(data);
    if (width > 0 && height > 0) {
        win->resize(ui::WindowDimension{
            .height = static_cast<unsigned int>(height),
            .width = static_cast<unsigned int>(width),
        });
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
    if (win->activation_protocol != nullptr && win->surface != nullptr) {
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

    if (new_keymap == nullptr) {
        return;
    }

    xkb_state *new_state = xkb_state_new(new_keymap);
    if (new_state == nullptr) {
        xkb_keymap_unref(new_keymap);
        return;
    }

    if (win->kb_state != nullptr) {
        xkb_state_unref(win->kb_state);
    }
    if (win->keymap != nullptr) {
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

    if (win->kb_state == nullptr) {
        return;
    }

    // XKB uses key + 8 for keycodes
    const xkb_keycode_t keycode = key + 8;
    const xkb_keysym_t keysym = xkb_state_key_get_one_sym(win->kb_state, keycode);

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
    case XKB_KEY_Delete:
        event.key = ui::KeyCode::Delete;
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

void pointer_enter_handler(void *data, wl_pointer *, uint32_t serial,
                           wl_surface *, wl_fixed_t , wl_fixed_t )
{
    PlatformWindow *win = static_cast<PlatformWindow *>(data);
    win->pointer_serial = serial;
    LOG_INFO("Pointer entered surface");
}

static void pointer_leave_handler(void *, wl_pointer *, uint32_t, wl_surface *)
{
    LOG_INFO("Pointer left surface");
}

static void pointer_motion_handler(void *, wl_pointer *, uint32_t, wl_fixed_t,
                                   wl_fixed_t)
{
    // Track mouse position if needed
}

static void pointer_button_handler(void *, wl_pointer *, uint32_t,
                                   uint32_t, uint32_t button,
                                   uint32_t state)
{
    LOG_INFO("Pointer button: %u, state: %u", button, state);
    // Button 272 = left click, state 1 = pressed
}

static void pointer_axis_handler(void *, wl_pointer *, uint32_t, uint32_t,
                                 wl_fixed_t)
{
    // Scroll events
}

static void pointer_frame_handler(void *, wl_pointer *) {}
static void pointer_axis_source_handler(void *, wl_pointer *,
                                        uint32_t)
{
}
static void pointer_axis_stop_handler(void *, wl_pointer *, uint32_t,
                                      uint32_t)
{
}
static void pointer_axis_discrete_handler(void *, wl_pointer *, uint32_t,
                                          int32_t)
{
}
static void pointer_axis_value120_handler(void *, wl_pointer *, uint32_t,
                                          int32_t)
{
}
static void pointer_axis_relative_direction_handler(void *, wl_pointer *,
                                                    uint32_t,
                                                    uint32_t)
{
}

static const wl_pointer_listener pointer_listener = {
    .enter = pointer_enter_handler,
    .leave = pointer_leave_handler,
    .motion = pointer_motion_handler,
    .button = pointer_button_handler,
    .axis = pointer_axis_handler,
    .frame = pointer_frame_handler,
    .axis_source = pointer_axis_source_handler,
    .axis_stop = pointer_axis_stop_handler,
    .axis_discrete = pointer_axis_discrete_handler,
    .axis_value120 = pointer_axis_value120_handler,
    .axis_relative_direction = pointer_axis_relative_direction_handler,
};

// Seat listener - handles input device capabilities
void seat_capabilities_handler(void *data, wl_seat *seat, uint32_t caps)
{
    PlatformWindow *win = static_cast<PlatformWindow *>(data);

    LOG_INFO("Seat capabilities: keyboard=%d, pointer=%d",
             (caps & WL_SEAT_CAPABILITY_KEYBOARD) ? 1 : 0,
             (caps & WL_SEAT_CAPABILITY_POINTER) ? 1 : 0);

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !win->keyboard) {
        win->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(win->keyboard, &keyboard_listener, win);
        LOG_INFO("Keyboard listener attached");
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && win->keyboard) {
        wl_keyboard_destroy(win->keyboard);
        win->keyboard = nullptr;
    }

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !win->pointer) {
        win->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(win->pointer, &pointer_listener, win);
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

// Buffer listener - handles buffer release events from compositor
static void buffer_release_handler(void* data, wl_buffer*)
{
    auto* buffer_entry = static_cast<WaylandBuffer*>(data);
    LOG_DEBUG("Buffer released: %dx%d", buffer_entry->width, buffer_entry->height);
    buffer_entry->state = WaylandBuffer::State::FREE;
}

static const wl_buffer_listener buffer_listener = {
    .release = buffer_release_handler
};

// PlatformWindow implementation

PlatformWindow::PlatformWindow(ui::RelScreenCoord,
                               ui::RelScreenCoord dimension)
    : display(nullptr), compositor(nullptr), surface(nullptr), seat(nullptr),
      keyboard(nullptr), pointer(nullptr), xdg_shell(nullptr),
      xdg_surface_obj(nullptr), toplevel(nullptr), activation_protocol(nullptr),
      activation_token(nullptr), xkb_ctx(nullptr), keymap(nullptr),
      kb_state(nullptr), shm(nullptr),
      current_buffer(nullptr), frame_counter(0),
      width(0), height(0), screen_width(1920), screen_height(1080)
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
    xdg_wm_base_add_listener(xdg_shell, &my_xdg_wm_base_listener, this);

    // Add seat listener for input
    if (seat) {
        wl_seat_add_listener(seat, &seat_listener, this);
    }

    // Critical: second roundtrip to receive output property events
    wl_display_roundtrip(display);
    if (output_infos.empty()) {
        throw std::runtime_error("No wayland output");
    }

    // Calculate window dimensions from screen resolution
    // (screen_width/height populated during first roundtrip via output
    // listener)
    const auto &[output, info] = *output_infos.begin();
    LOG_INFO("Using first output by default");
    width = static_cast<unsigned int>(info.width * dimension.x);
    height = static_cast<unsigned int>(info.height * dimension.y);
    LOG_INFO("Wayland window: %dx%d", width, height);

    // Create surface
    surface = wl_compositor_create_surface(compositor);
    if (!surface) {
        throw std::runtime_error("Failed to create Wayland surface");
    }

    // Create XDG surface and toplevel
    xdg_surface_obj = xdg_wm_base_get_xdg_surface(xdg_shell, surface);
    xdg_surface_add_listener(xdg_surface_obj, &my_xdg_surface_listener, this);

    toplevel = xdg_surface_get_toplevel(xdg_surface_obj);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, this);

    // Set window properties
    xdg_toplevel_set_title(toplevel, "khala");
    xdg_toplevel_set_app_id(toplevel, "com.khala.launcher");

    // Set window size constraints based on actual screen resolution
    xdg_toplevel_set_min_size(toplevel, static_cast<int>(width), static_cast<int>(height));
    xdg_toplevel_set_max_size(toplevel, static_cast<int>(width), static_cast<int>(height));

    // Commit surface to trigger configure
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    // Request focus using XDG Activation
    const char *env_token = std::getenv("XDG_ACTIVATION_TOKEN");
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

    // Reserve buffer pool capacity
    buffer_pool.reserve(MAX_BUFFERS);
}

PlatformWindow::~PlatformWindow()
{
    // Cleanup in reverse order of creation

    // Clean up buffer pool
    for (auto& buf : buffer_pool) {
        destroy_buffer(buf);
    }
    buffer_pool.clear();
    current_buffer = nullptr;

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
    for (auto [output, _] : output_infos) {
        wl_output_destroy(output);
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

    if (cached_context) {
        cairo_destroy(cached_context);
        cached_context = nullptr;
    }

    if (cached_surface) {
        // Don't destroy - already destroyed in buffer pool cleanup
        cached_surface = nullptr;
    }
}

// Buffer pool management methods

void PlatformWindow::create_wl_buffer(WaylandBuffer& buf, unsigned int w, unsigned int h)
{
    const auto stride = w * 4;
    const auto buffer_size = stride * h;

    // Create anonymous file
    buf.shm_fd = create_anonymous_file(buffer_size);
    if (buf.shm_fd < 0) {
        throw std::runtime_error("Failed to create anonymous file");
    }

    // Map shared memory
    buf.shm_data = mmap(nullptr, buffer_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, buf.shm_fd, 0);
    if (buf.shm_data == MAP_FAILED) {
        close(buf.shm_fd);
        throw std::runtime_error("Failed to mmap buffer");
    }

    // Create wl_shm_pool and wl_buffer
    wl_shm_pool* pool = wl_shm_create_pool(shm, buf.shm_fd, static_cast<int32_t>(buffer_size));
    if (!pool) {
        munmap(buf.shm_data, buffer_size);
        close(buf.shm_fd);
        throw std::runtime_error("Failed to create SHM pool");
    }

    buf.wl_buffer_obj = wl_shm_pool_create_buffer(
        pool, 0, static_cast<int32_t>(w), static_cast<int32_t>(h), static_cast<int32_t>(stride), WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);  // Can destroy pool immediately

    if (!buf.wl_buffer_obj) {
        munmap(buf.shm_data, buffer_size);
        close(buf.shm_fd);
        throw std::runtime_error("Failed to create buffer");
    }

    // Add release listener - CRITICAL for proper lifecycle!
    wl_buffer_add_listener(buf.wl_buffer_obj, &buffer_listener, &buf);

    // Create Cairo surface
    buf.cairo_surface = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(buf.shm_data),
        CAIRO_FORMAT_ARGB32, static_cast<int32_t>(w), static_cast<int32_t>(h), static_cast<int32_t>(stride));

    if (cairo_surface_status(buf.cairo_surface) != CAIRO_STATUS_SUCCESS) {
        wl_buffer_destroy(buf.wl_buffer_obj);
        munmap(buf.shm_data, buffer_size);
        close(buf.shm_fd);
        throw std::runtime_error("Failed to create Cairo surface");
    }

    // Store metadata
    buf.width = w;
    buf.height = h;
    buf.size = buffer_size;
    buf.state = WaylandBuffer::State::FREE;
    buf.last_used_frame = 0;

    LOG_DEBUG("Created buffer: %dx%d (%u bytes)", w, h, buffer_size);
}

void PlatformWindow::destroy_buffer(WaylandBuffer& buf)
{
    LOG_DEBUG("Destroying buffer: %dx%d", buf.width, buf.height);

    if (buf.cairo_surface) {
        cairo_surface_destroy(buf.cairo_surface);
        buf.cairo_surface = nullptr;
    }

    if (buf.wl_buffer_obj) {
        wl_buffer_destroy(buf.wl_buffer_obj);
        buf.wl_buffer_obj = nullptr;
    }

    if (buf.shm_data) {
        munmap(buf.shm_data, buf.size);
        buf.shm_data = nullptr;
    }

    if (buf.shm_fd >= 0) {
        close(buf.shm_fd);
        buf.shm_fd = -1;
    }
}

WaylandBuffer* PlatformWindow::find_free_buffer(unsigned int buf_width, unsigned int buf_height)
{
    for (auto& buf : buffer_pool) {
        if (buf.state == WaylandBuffer::State::FREE &&
            buf.width == buf_width &&
            buf.height == buf_height) {
            return &buf;
        }
    }
    return nullptr;
}

void PlatformWindow::cleanup_old_buffers()
{
    // Find FREE buffers with wrong dimensions
    std::vector<WaylandBuffer*> candidates;

    for (auto& buf : buffer_pool) {
        if (buf.state == WaylandBuffer::State::FREE &&
            (buf.width != width || buf.height != height)) {
            candidates.push_back(&buf);
        }
    }

    // Sort by last_used_frame (oldest first)
    std::sort(candidates.begin(), candidates.end(),
        [](WaylandBuffer* a, WaylandBuffer* b) {
            return a->last_used_frame < b->last_used_frame;
        });

    // Destroy oldest to make room
    if (!candidates.empty()) {
        destroy_buffer(*candidates[0]);
        // Remove from pool
        buffer_pool.erase(
            std::remove_if(buffer_pool.begin(), buffer_pool.end(),
                [&](WaylandBuffer& b) { return &b == candidates[0]; }),
            buffer_pool.end());
        LOG_DEBUG("Cleaned up old buffer, pool size now: %zu", buffer_pool.size());
    }
}

WaylandBuffer* PlatformWindow::allocate_buffer(unsigned int buf_width, unsigned int buf_height)
{
    // Step 1: Try to find existing FREE buffer with matching dimensions
    WaylandBuffer* buf = find_free_buffer(buf_width, buf_height);
    if (buf != nullptr) {
        LOG_DEBUG("Reusing existing buffer: %dx%d", buf_width, buf_height);
        buf->state = WaylandBuffer::State::DRAWING;
        buf->last_used_frame = frame_counter++;
        return buf;
    }

    // Step 2: If pool not full, create new buffer
    if (buffer_pool.size() < MAX_BUFFERS) {
        LOG_DEBUG("Creating new buffer in pool: %dx%d (pool size: %zu)",
                  buf_width, buf_height, buffer_pool.size());
        buffer_pool.emplace_back();
        WaylandBuffer& new_buf = buffer_pool.back();

        create_wl_buffer(new_buf, buf_width, buf_height);
        new_buf.state = WaylandBuffer::State::DRAWING;
        new_buf.last_used_frame = frame_counter++;

        return &new_buf;
    }

    // Step 3: Pool is full - try to destroy oldest FREE buffer with wrong dimensions
    cleanup_old_buffers();

    // Step 4: Try again after cleanup
    buf = find_free_buffer(buf_width, buf_height);
    if (buf != nullptr) {
        buf->state = WaylandBuffer::State::DRAWING;
        buf->last_used_frame = frame_counter++;
        return buf;
    }

    // Step 5: No free buffers - need to wait for release (edge case)
    LOG_WARNING("All buffers busy, waiting for release...");

    // Dispatch events to try to get a release
    wl_display_roundtrip(display);
    return allocate_buffer(buf_width, buf_height);  // Retry
}

void PlatformWindow::resize(const ui::WindowDimension& dimensions)
{
    height = dimensions.height;
    width = dimensions.width;
    // Note: Wayland clients don't resize directly - the compositor handles it
    // We just track the size for our buffer allocation
}

cairo_surface_t *PlatformWindow::get_cairo_surface()
{
    // Check if we have current buffer with matching dimensions
    if (surface_cache_valid()) {
        // Cache hit - return existing buffer
        return current_buffer->cairo_surface;
    }

    // Cache miss - clear old reference (don't destroy, pool manages lifecycle)
    if (cached_surface != nullptr) {
        cached_surface = nullptr;
    }

    // Allocate new buffer from pool
    cached_surface = create_cairo_surface(height, width);

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
    
    cairo_surface_t *current_surface = get_cairo_surface();

    // Create context if we don't have one
    cached_context = cairo_create(current_surface);
    if (cached_context == nullptr || cairo_status(cached_context) != CAIRO_STATUS_SUCCESS) {
        const int status = cached_context != nullptr ? cairo_status(cached_context) : -1;
        if (cached_context != nullptr) {
            cairo_destroy(cached_context);
            cached_context = nullptr;
        }
        throw std::runtime_error("Failed to create Cairo context, status: " + std::to_string(status));
    }
    LOG_DEBUG("Created new Cairo context");

    return cached_context;
}

bool PlatformWindow::surface_cache_valid() const
{
    if (!current_buffer) {
        LOG_DEBUG("Surface cache miss: no buffer");
        return false;
    }
    
    if (current_buffer->width != width || current_buffer->height != height) {
        LOG_DEBUG("Surface cache miss: dimensions changed (cached: %ux%u "
                  "window: %ux%u)",
                  current_buffer->width, current_buffer->height, width, height);
        return false;
    }
    if (current_buffer->state != WaylandBuffer::State::DRAWING) {
        LOG_DEBUG("Surface cache miss: current_buffer->state != WaylandBuffer::State::DRAWING");
        return false;
    }
    
    if (cached_surface == nullptr) {
        LOG_DEBUG("Surface cache miss: cached_surface == nullptr");
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

cairo_surface_t *PlatformWindow::create_cairo_surface(unsigned int h, unsigned int w)
{
    LOG_DEBUG("Creating Cairo surface: %ux%u", w, h);

    // Update window dimensions
    width = w;
    height = h;

    // Allocate or reuse buffer from pool
    current_buffer = allocate_buffer(w, h);

    if (!current_buffer) {
        throw std::runtime_error("Failed to allocate buffer from pool");
    }

    return current_buffer->cairo_surface;
}

std::vector<ui::UserInputEvent> PlatformWindow::get_input_events(bool blocking)
{
    if (blocking) {
        wl_display_dispatch(display);
    } else {
        // Flush any pending outgoing requests
        wl_display_flush(display);

        // Acquire read lock
        while (wl_display_prepare_read(display) != 0) {
            // Acquire failed, need to dispatch pending events first
            wl_display_dispatch_pending(display);
        }

        // Check if socket has data (non-blocking with 0 timeout)
        struct pollfd pfd = {wl_display_get_fd(display), POLLIN, 0};
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            // Actually read events from the inbound queue
            wl_display_read_events(display);
        } else {
            // Release read lock
            wl_display_cancel_read(display);
        }

        // Process any events now in the queue
        wl_display_dispatch_pending(display);
    }

    std::vector<ui::UserInputEvent> events = std::move(pending_events);
    pending_events.clear();
    return events;
}

void PlatformWindow::commit_surface()
{
    // Get the cairo surface to flush
    cairo_surface_t* cairo_surface = get_cairo_surface();
    if (cairo_surface == nullptr) {
        throw std::runtime_error("Cannot commit surface: no cairo surface available");
    }

    // Flush cairo operations to the underlying buffer
    cairo_surface_flush(cairo_surface);

    // Commit buffer to Wayland compositor
    if (current_buffer == nullptr || surface == nullptr) {
        throw std::runtime_error("Cannot commit surface: missing buffer or surface");
    }

    // Transition: DRAWING -> SUBMITTED
    current_buffer->state = WaylandBuffer::State::SUBMITTED;

    wl_surface_attach(surface, current_buffer->wl_buffer_obj, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height));
    wl_surface_commit(surface);

    LOG_DEBUG("Committed surface to compositor: %dx%d",
              current_buffer->width, current_buffer->height);
}

#endif // PLATFORM_WAYLAND
