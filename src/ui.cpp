#include "ui.h"
#include "utility.h"
#include "fuzzy.h"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <cairo-xlib.h>
#include <cairo.h>

#include "pango/pango-font.h"
#include "pango/pango-layout.h"
#include "pango/pango-types.h"
#include <pango/pangocairo.h>

#include <algorithm>
#include <ranges>
#include <string>

namespace ui
{

struct MonitorInfo {
    int width;
    int height;
    int x;
    int y;
    bool found;
};

MonitorInfo get_primary_monitor_xrandr(Display* display, int screen) {
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
    XRRScreenResources* screen_resources = XRRGetScreenResources(display, root);
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
            XRROutputInfo* output_info = XRRGetOutputInfo(display, screen_resources, screen_resources->outputs[i]);
            if (output_info && output_info->connection == RR_Connected && output_info->crtc) {
                primary = screen_resources->outputs[i];
                XRRFreeOutputInfo(output_info);
                break;
            }
            if (output_info) XRRFreeOutputInfo(output_info);
        }
    }
    
    if (primary != None) {
        XRROutputInfo* output_info = XRRGetOutputInfo(display, screen_resources, primary);
        if (output_info && output_info->crtc) {
            XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(display, screen_resources, output_info->crtc);
            if (crtc_info) {
                info.width = crtc_info->width;
                info.height = crtc_info->height;
                info.x = crtc_info->x;
                info.y = crtc_info->y;
                info.found = true;
                
                printf("Primary monitor found: %dx%d at (%d,%d)\n", 
                       info.width, info.height, info.x, info.y);
                
                XRRFreeCrtcInfo(crtc_info);
            }
        }
        if (output_info) XRRFreeOutputInfo(output_info);
    }
    
    XRRFreeScreenResources(screen_resources);
    return info;
}

std::string create_highlighted_markup(const std::string& text, const std::vector<size_t>& match_positions) {
    if (match_positions.empty()) {
        // No highlighting needed, escape the text for markup
        std::string escaped;
        for (char c : text) {
            if (c == '&') escaped += "&amp;";
            else if (c == '<') escaped += "&lt;";
            else if (c == '>') escaped += "&gt;";
            else escaped += c;
        }
        return escaped;
    }
    
    std::string result;
    size_t match_idx = 0;
    
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        
        // Check if this position should be highlighted
        bool should_highlight = (match_idx < match_positions.size() && 
                                match_positions[match_idx] == i);
        
        if (should_highlight) {
            result += "<b>";
        }
        
        // Escape special markup characters
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else result += c;
        
        if (should_highlight) {
            result += "</b>";
            match_idx++;
        }
    }
    
    return result;
}


int calculate_actual_input_height(const Config& config, int screen_height) {
    return static_cast<int>(screen_height * config.input_height_ratio);
}

int calculate_actual_item_height(const Config& config, int screen_height) {
    return static_cast<int>(screen_height * config.item_height_ratio);
}

XWindow::XWindow(const Config& config) {
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
        // If aspect ratio suggests multiple monitors, estimate primary monitor size
        const double aspect_ratio = static_cast<double>(total_width) / total_height;
        
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
    
    // Calculate window dimensions based on primary screen size and config ratios
    width = static_cast<int>(primary_screen_width * config.width_ratio);
    const int input_height = calculate_actual_input_height(config, primary_screen_height);
    const int item_height = calculate_actual_item_height(config, primary_screen_height);
    height = input_height + (config.max_visible_items * item_height);
    
    // Center the window properly: position is relative to center, not top-left corner
    // Also account for monitor offset in multi-monitor setups
    const int x = primary_screen_x + static_cast<int>(primary_screen_width * config.x_position - width / 2);
    const int y = primary_screen_y + static_cast<int>(primary_screen_height * config.y_position - height / 2);
    
    // Debug output
    printf("Primary monitor: %dx%d at (%d,%d)\n", 
           primary_screen_width, primary_screen_height, primary_screen_x, primary_screen_y);
    printf("Window: %dx%d at (%d,%d)\n", width, height, x, y);
    
    XVisualInfo vinfo;
    XMatchVisualInfo(display, screen, 32, TrueColor, &vinfo);
    
    colormap = XCreateColormap(
        display, RootWindow(display, screen), vinfo.visual, AllocNone);
    
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.colormap = colormap;
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | FocusChangeMask;
    
    window = XCreateWindow(
        display, RootWindow(display, screen), x, y, width, height,
        0, vinfo.depth, InputOutput, vinfo.visual,
        CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel | CWEventMask,
        &attrs);
    
    Atom windowType = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom dialogType = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, window, windowType, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)&dialogType, 1);
    
    Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    Atom stateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(display, window, stateAtom, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)&stateAbove, 1);
    
    XMapRaised(display, window);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XFlush(display);
}

XWindow::~XWindow() {
    if (display) {
        if (window) XDestroyWindow(display, window);
        if (colormap) XFreeColormap(display, colormap);
        XCloseDisplay(display);
    }
}

int calculate_window_height(const Config& config, const State& state, int screen_height) {
    const size_t item_count = state.context_menu_open
        ? state.get_selected_item().actions.size()
        : state.items.size();
    const size_t visible_items = std::min(item_count, config.max_visible_items);
    const int input_height = calculate_actual_input_height(config, screen_height);
    const int item_height = calculate_actual_item_height(config, screen_height);
    return input_height + (visible_items * item_height);
}

Item State::get_selected_item() const { return items.at(selected_item_index); }

Action State::get_selected_action() const
{
    return get_selected_item().actions.at(selected_action_index);
}

void State::set_error(const std::string& message)
{
    error_message = message + " (Esc to clear)";
}

void State::clear_error()
{
    error_message = std::nullopt;
}

bool State::has_error() const
{
    return error_message.has_value();
}

enum class Corner : uint8_t {
    NoCorners = 0,
    TopLeft = 1 << 0,
    TopRight = 1 << 1,
    BottomRight = 1 << 2,
    BottomLeft = 1 << 3,
    All = TopLeft | TopRight | BottomRight | BottomLeft
};

// Bitwise operators for Corner enum
constexpr Corner operator|(Corner a, Corner b)
{
    return static_cast<Corner>(static_cast<uint8_t>(a) |
                               static_cast<uint8_t>(b));
}

constexpr bool operator&(Corner a, Corner b)
{
    return static_cast<uint8_t>(a) & static_cast<uint8_t>(b);
}

static void draw_rounded_rect(cairo_t *cr, double x, double y, double width,
                              double height, double radius, Corner corners)
{
    const double degrees = G_PI / 180.0;

    cairo_new_sub_path(cr);

    // Top-right corner
    if (corners & Corner::TopRight) {
        cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees,
                  0 * degrees);
    } else {
        cairo_move_to(cr, x + width, y);
    }

    // Bottom-right corner
    if (corners & Corner::BottomRight) {
        cairo_arc(cr, x + width - radius, y + height - radius, radius,
                  0 * degrees, 90 * degrees);
    } else {
        cairo_line_to(cr, x + width, y + height);
    }

    // Bottom-left corner
    if (corners & Corner::BottomLeft) {
        cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees,
                  180 * degrees);
    } else {
        cairo_line_to(cr, x, y + height);
    }

    // Top-left corner
    if (corners & Corner::TopLeft) {
        cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees,
                  270 * degrees);
    } else {
        cairo_line_to(cr, x, y);
    }

    cairo_close_path(cr);
}

Event process_input_events(Display *display, State &state)
{
    XEvent event;

    Event out_event = Event::NoEvent;

    // Always wait for at least one event to avoid busy looping
    XNextEvent(display, &event);
    
    // Process the first event
    goto process_event;
    
    // Then process any additional pending events without blocking
    while (XPending(display) > 0) {
        XNextEvent(display, &event);
        
    process_event:

        if (event.type == Expose) {
            if (event.xexpose.count == 0) {
            }
        } else if (event.type == ButtonPress) {
            // Regain focus when clicked
            XSetInputFocus(display, event.xbutton.window, RevertToParent, CurrentTime);
        } else if (event.type == KeyPress) {
            const KeySym keysym = XLookupKeysym(&event.xkey, 0);

            if (keysym == XK_Escape) {
                out_event = Event::ExitRequested;
            } else if (keysym == XK_Up) {
                // Move selection up
                if (state.context_menu_open) {
                    if (state.selected_action_index > 0) {
                        state.selected_action_index--;
                        out_event = Event::SelectionChanged;
                    }
                } else {
                    if (state.selected_item_index > 0) {
                        state.selected_item_index--;
                        out_event = Event::SelectionChanged;
                    }
                }
            } else if (keysym == XK_Down) {
                // Move selection down
                if (state.context_menu_open) {
                    const size_t max_action_index =
                        state.get_selected_item().actions.size() - 1;
                    if (state.selected_action_index < max_action_index) {
                        state.selected_action_index++;
                        out_event = Event::SelectionChanged;
                    }
                } else {
                    if (state.selected_item_index < state.items.size() - 1) {
                        state.selected_item_index++;
                        out_event = Event::SelectionChanged;
                    }
                }
            } else if (keysym == XK_Tab) {
                // Open context menu
                if (!state.context_menu_open && !state.items.empty() &&
                    !state.get_selected_item().actions.empty()) {
                    state.context_menu_open = true;
                    state.selected_action_index = 0;
                    out_event = Event::ContextMenuToggled;
                }
            } else if (keysym == XK_Left) {
                if (state.context_menu_open) {
                    // Close context menu
                    state.context_menu_open = false;
                    out_event = Event::ContextMenuToggled;
                } else {
                    // Move cursor left
                    if (state.cursor_position > 0) {
                        state.cursor_position--;
                        out_event = Event::CursorPositionChanged;
                    }
                }
            } else if (keysym == XK_Right) {
                // Move cursor right (only when not in context menu)
                if (!state.context_menu_open &&
                    state.cursor_position < state.input_buffer.size()) {
                    state.cursor_position++;
                    out_event = Event::CursorPositionChanged;
                }
            } else if (keysym == XK_Home) {
                // Jump to beginning
                if (!state.context_menu_open) {
                    state.cursor_position = 0;
                    out_event = Event::CursorPositionChanged;
                }
            } else if (keysym == XK_End) {
                // Jump to end
                if (!state.context_menu_open) {
                    state.cursor_position = state.input_buffer.size();
                    out_event = Event::CursorPositionChanged;
                }
            } else if (keysym == XK_Return) {
                out_event = Event::ActionRequested;
            } else if (keysym == XK_BackSpace) {
                // Handle backspace at cursor position
                if (!state.input_buffer.empty() && state.cursor_position > 0) {
                    state.input_buffer.erase(state.cursor_position - 1, 1);
                    state.cursor_position--;
                    out_event = Event::InputChanged;
                }
            } else {
                // Handle regular character input at cursor position
                std::array<char, 32> char_buffer;
                const int len =
                    XLookupString(&event.xkey, char_buffer.data(),
                                  char_buffer.size(), nullptr, nullptr);
                if (len > 0) {
                    char_buffer[len] = '\0';
                    // Only add printable characters
                    for (int i = 0; i < len; ++i) {
                        if (char_buffer[i] >= 32 && char_buffer[i] < 127) {
                            state.input_buffer.insert(state.cursor_position, 1,
                                                      char_buffer[i]);
                            state.cursor_position++;
                            out_event = Event::InputChanged;
                        }
                    }
                }
            }
            break;
        }
    }
    return out_event;
}

void draw(XWindow& window, const Config& config, const State &state)
{
        // Calculate actual heights based on screen size
        const int input_height = calculate_actual_input_height(config, window.screen_height);
        const int item_height = calculate_actual_item_height(config, window.screen_height);
        
        // Calculate window height based on item count
        const int new_height = calculate_window_height(config, state, window.screen_height);
        if (new_height != window.height) {
            XResizeWindow(window.display, window.window, window.width, new_height);
            window.height = new_height;
        }

    // Get the window's visual (which should be ARGB for transparency)
    XWindowAttributes window_attrs;
    XGetWindowAttributes(window.display, window.window, &window_attrs);
    Visual *visual = window_attrs.visual;

    // Create Cairo surface for X11 window
    cairo_surface_t *surface =
        cairo_xlib_surface_create(window.display, window.window, visual, window.width, window.height);
    const defer cleanup_surface(
        [surface]() noexcept { cairo_surface_destroy(surface); });

    // Create Cairo context
    cairo_t *cr = cairo_create(surface);
    const defer cleanup_cr([cr]() noexcept { cairo_destroy(cr); });

    // Create Pango layout for text rendering
    PangoLayout *layout = pango_cairo_create_layout(cr);
    const defer cleanup_layout([layout]() noexcept { g_object_unref(layout); });

    // Clear everything with transparent background
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Draw entire window background with rounded corners
    const double corner_radius = 8.0;
    const double border_width = 3.0;

    // Fill entire window with input color (grey)
    draw_rounded_rect(cr, 0, 0, window.width, window.height, corner_radius, Corner::All);
    cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
    cairo_fill(cr);

    // Draw white background for dropdown area if there are items
    if (window.height > input_height) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        draw_rounded_rect(cr, 0, input_height, window.width, window.height - input_height,
                          corner_radius,
                          Corner::BottomLeft | Corner::BottomRight);
        cairo_fill(cr);
    }

    // Draw white border around entire window
    draw_rounded_rect(cr, 0, 0, window.width, window.height, corner_radius, Corner::All);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, border_width);
    cairo_stroke(cr);

    // Set font for launcher
    PangoFontDescription *font_desc = pango_font_description_from_string(
        (config.font_name + " " + std::to_string(config.font_size)).c_str());
    const defer cleanup_font(
        [font_desc]() noexcept { pango_font_description_free(font_desc); });
    pango_layout_set_font_description(layout, font_desc);

    // Draw search prompt and buffer
    std::string display_text;
    if (state.context_menu_open) {
        // Show selected item title when in context menu
        display_text = state.get_selected_item().title + " › Actions";
    } else {
        display_text = state.input_buffer;
        if (state.input_buffer.empty()) {
            display_text = "Search files... (prefix > for utility actions, ! for applications)";
        }
    }

    pango_layout_set_text(layout, display_text.c_str(), -1);
    pango_layout_set_attributes(layout, nullptr);

    // Get text dimensions for vertical centering
    int text_width;
    int text_height;
    pango_layout_get_size(layout, &text_width, &text_height);
    const double text_y = (input_height - (text_height / PANGO_SCALE)) / 2.0;

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_move_to(cr, 10, text_y);
    pango_cairo_show_layout(cr, layout);

    // Draw cursor at cursor position when not in context menu
    if (!state.context_menu_open) {
        // Get width of text up to cursor position
        const std::string text_before_cursor =
            state.input_buffer.substr(0, state.cursor_position);
        pango_layout_set_text(layout, text_before_cursor.c_str(), -1);
        int cursor_x_offset;
        int cursor_height;
        pango_layout_get_size(layout, &cursor_x_offset, &cursor_height);

        // Draw cursor line
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        const double cursor_x = 10 + (cursor_x_offset / PANGO_SCALE);
        cairo_move_to(cr, cursor_x, text_y);
        cairo_line_to(cr, cursor_x, text_y + (text_height / PANGO_SCALE));
        cairo_stroke(cr);

        // Restore original text for layout
        pango_layout_set_text(layout, display_text.c_str(), -1);
    }

    struct DropdownItem {
        std::string title;
        std::string description;
        std::vector<size_t> title_match_positions;
        std::vector<size_t> description_match_positions;
    };

    auto to_dropdown = [&state](const auto &x) -> DropdownItem {
        if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Item>) {
            return DropdownItem{
                .title = x.title, 
                .description = x.description, 
                .title_match_positions = fuzzy::fuzzy_match(x.title, state.current_query),
                .description_match_positions = fuzzy::fuzzy_match(x.description, state.current_query)
            };
        } else {
            return DropdownItem{
                .title = x.title, 
                .description = x.description, 
                .title_match_positions = fuzzy::fuzzy_match(x.title, state.current_query),
                .description_match_positions = fuzzy::fuzzy_match(x.description, state.current_query)
            };
        }
    };

    auto [dropdown_items, selection_index] =
        [&]() -> std::pair<std::vector<DropdownItem>, size_t> {
        if (state.context_menu_open) {
            auto transformed = state.get_selected_item().actions |
                               std::views::transform(to_dropdown);
            return {std::vector<DropdownItem>(transformed.begin(),
                                              transformed.end()),
                    state.selected_action_index};
        } else {
            auto transformed = state.items | std::views::transform(to_dropdown);
            return {std::vector<DropdownItem>(transformed.begin(),
                                              transformed.end()),
                    state.selected_item_index};
        }
    }();

    // Draw dropdown items
    for (size_t i = 0; i < dropdown_items.size(); ++i) {
        const int y_pos = input_height + (i * item_height);

        // Draw selection highlight
        if (i == selection_index) {
            cairo_set_source_rgb(cr, 0.3, 0.6, 1.0); // Blue highlight

            // Use rounded bottom corners if this is the last item
            const bool is_last_item = (i == dropdown_items.size() - 1);
            if (is_last_item) {
                draw_rounded_rect(cr, 0, y_pos, window.width, item_height,
                                  corner_radius,
                                  Corner::BottomLeft | Corner::BottomRight);
            } else {
                draw_rounded_rect(cr, 0, y_pos, window.width, item_height, 0,
                                  Corner::NoCorners);
            }
            cairo_fill(cr);
        }

        // Set text color (white on selected, black on normal)
        if (i == selection_index) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        } else {
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        }

        // Draw the title text
        pango_layout_set_text(layout, dropdown_items.at(i).title.c_str(), -1);
        // Draw icon and filename (main text) with highlighting - center vertically within item_height
        const std::string highlighted_title = create_highlighted_markup(dropdown_items.at(i).title, dropdown_items.at(i).title_match_positions);
        pango_layout_set_markup(layout, highlighted_title.c_str(), -1);
        int text_width_unused, text_height;
        pango_layout_get_size(layout, &text_width_unused, &text_height);
        const double text_y_centered = y_pos + (item_height - (text_height / PANGO_SCALE)) / 2.0;
        cairo_move_to(cr, 15, text_y_centered);
        pango_cairo_show_layout(cr, layout);

        // Draw description to the right of the title in subtle grey
        if (!dropdown_items.at(i).description.empty()) {
            // Get the width of the title text
            int title_width;
            int title_height;
            pango_layout_get_size(layout, &title_width, &title_height);

            // Calculate available width for description
            const int spacing = 10;
            const int left_margin = 15;
            const int right_margin = 15;
            const int available_width = window.width - left_margin -
                                        (title_width / PANGO_SCALE) - spacing -
                                        right_margin;

            // Set subtle grey color for description
            if (i == selection_index) {
                cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
            } else {
                cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
            }

            // Set description text with highlighting and middle ellipsization
            const std::string highlighted_description = create_highlighted_markup(dropdown_items.at(i).description, dropdown_items.at(i).description_match_positions);
            pango_layout_set_markup(layout, highlighted_description.c_str(), -1);
            pango_layout_set_width(layout, available_width * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);

            // Draw description with some spacing after the title
            cairo_move_to(cr,
                          left_margin + (title_width / PANGO_SCALE) + spacing,
                          text_y_centered);
            pango_cairo_show_layout(cr, layout);

            // Reset layout settings for next iteration
            pango_layout_set_width(layout, -1);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        }

        // Reset font for next iteration
        pango_layout_set_font_description(layout, font_desc);
    }

    // Flush to display
    cairo_surface_flush(surface);
}

} // namespace ui