#include "fuzzy.h"
#include "glib-object.h"
#include "indexer.h"
#include "pango/pango-font.h"
#include "pango/pango-layout.h"
#include "pango/pango-types.h"
#include "utility.h"

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <cairo-xlib.h>
#include <cairo.h>
#include <pango/pangocairo.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static constexpr int WIDTH = 600;
static constexpr int INPUT_HEIGHT = 40;
static constexpr int OPTION_HEIGHT = 30;
static constexpr int MAX_VISIBLE_OPTIONS = 8;
static constexpr int DROPDOWN_HEIGHT = MAX_VISIBLE_OPTIONS * OPTION_HEIGHT;
static constexpr int TOTAL_HEIGHT = INPUT_HEIGHT + DROPDOWN_HEIGHT;

// Global cache of indexed paths
static PackedStrings g_indexed_paths;
static bool g_index_loaded = false;

// Initialize index on first use
static void ensure_index_loaded()
{
    if (!g_index_loaded) {
        const fs::path home = std::getenv("HOME");
        printf("Loading index for %s...\n", home.c_str());

        // Scan filesystem and cache results
        g_indexed_paths = indexer::scan_filesystem_parallel(home);
        g_index_loaded = true;

        printf("Loaded %zu files\n", g_indexed_paths.size());
    }
}

namespace
{
void draw(Display *display, Window window, int width, int height,
          const std::string &query_buffer,
          const std::vector<RankResult> &results, int selected_index)
{
    // Get the default visual
    const int screen = DefaultScreen(display);
    Visual *visual = DefaultVisual(display, screen);

    // Create Cairo surface for X11 window
    cairo_surface_t *surface =
        cairo_xlib_surface_create(display, window, visual, width, height);
    const defer cleanup_surface(
        [surface]() noexcept { cairo_surface_destroy(surface); });

    // Create Cairo context
    cairo_t *cr = cairo_create(surface);
    const defer cleanup_cr([cr]() noexcept { cairo_destroy(cr); });

    // Create Pango layout for text rendering
    PangoLayout *layout = pango_cairo_create_layout(cr);
    const defer cleanup_layout([layout]() noexcept { g_object_unref(layout); });

    // Clear background with white
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Set font for launcher
    PangoFontDescription *font_desc =
        pango_font_description_from_string("Sans 12");
    const defer cleanup_font(
        [font_desc]() noexcept { pango_font_description_free(font_desc); });
    pango_layout_set_font_description(layout, font_desc);

    // Draw search input area
    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95); // Light gray background
    cairo_rectangle(cr, 0, 0, width, INPUT_HEIGHT);
    cairo_fill(cr);

    // Draw search prompt and buffer
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_move_to(cr, 10, 15);

    std::string display_text = "> " + query_buffer;
    if (query_buffer.empty()) {
        display_text += "(type to search...)";
    }

    pango_layout_set_text(layout, display_text.c_str(), -1);
    pango_layout_set_attributes(layout, nullptr);
    pango_cairo_show_layout(cr, layout);

    // Draw cursor after text if there's content
    if (!query_buffer.empty()) {
        int text_width;
        int text_height;
        pango_layout_get_size(layout, &text_width, &text_height);

        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_move_to(cr, 10 + (text_width / PANGO_SCALE), 15);
        cairo_line_to(cr, 10 + (text_width / PANGO_SCALE),
                      15 + (text_height / PANGO_SCALE));
        cairo_stroke(cr);
    }

    // Draw separator line
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_move_to(cr, 0, INPUT_HEIGHT);
    cairo_line_to(cr, width, INPUT_HEIGHT);
    cairo_stroke(cr);

    // Draw dropdown options
    const int visible_count =
        std::min(static_cast<int>(results.size()), MAX_VISIBLE_OPTIONS);
    for (int i = 0; i < visible_count; ++i) {
        const int y_pos = INPUT_HEIGHT + (i * OPTION_HEIGHT);

        // Draw selection highlight
        if (i == selected_index) {
            cairo_set_source_rgb(cr, 0.3, 0.6, 1.0); // Blue highlight
            cairo_rectangle(cr, 0, y_pos, width, OPTION_HEIGHT);
            cairo_fill(cr);
        }

        // Set text color (white on selected, black on normal)
        if (i == selected_index) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        } else {
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        }

        // Draw filename (main text)
        cairo_move_to(cr, 15, y_pos + 8);
        pango_layout_set_text(layout,
                              g_indexed_paths.at(results[i].index).data(), -1);
        pango_cairo_show_layout(cr, layout);

        // Reset font for next iteration
        pango_layout_set_font_description(layout, font_desc);

        // Draw separator between options
        if (i < visible_count - 1) {
            cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
            cairo_move_to(cr, 10, y_pos + OPTION_HEIGHT);
            cairo_line_to(cr, width - 10, y_pos + OPTION_HEIGHT);
            cairo_stroke(cr);
        }
    }

    // Flush to display
    cairo_surface_flush(surface);
}

} // namespace

int main()
{
    // Open connection to X server
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }
    const defer cleanup_display(
        [display]() noexcept { XCloseDisplay(display); });

    const int screen = DefaultScreen(display);

    // Get screen dimensions for centering
    const int screen_width = DisplayWidth(display, screen);
    const int screen_height = DisplayHeight(display, screen);

    const int x = (screen_width - WIDTH) / 2;
    const int y = screen_height / 4;

    // Create window attributes
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = WhitePixel(display, screen);
    attrs.border_pixel = BlackPixel(display, screen);
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask;

    // Create the window
    const Window window = XCreateWindow(
        display, RootWindow(display, screen), x, y, WIDTH, TOTAL_HEIGHT, 2,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &attrs);

    const defer cleanup_window(
        [display, window]() noexcept { XDestroyWindow(display, window); });

    // Set window type hint
    const Atom windowType = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    const Atom dialogType =
        XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, window, windowType, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&dialogType, 1);

    // Set window to stay on top
    const Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    const Atom stateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(display, window, stateAtom, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&stateAbove, 1);

    // Map (show) the window
    XMapRaised(display, window);

    // Grab keyboard focus
    XSetInputFocus(display, window, RevertToParent, CurrentTime);

    XFlush(display);

    printf("Launcher window opened. Press ESC to close.\n");

    // Event loop
    XEvent event;
    bool running = true;
    std::string query_buffer;
    int selected_index = 0;
    bool needs_redraw = true;

    ensure_index_loaded();

    std::vector<RankResult> current_results;
    auto rerank = [&current_results, &query_buffer]() {
        auto tik = std::chrono::steady_clock::now();
        current_results = rank_parallel(
            g_indexed_paths,
            [&query_buffer](std::string_view path) {
                return fuzzy::fuzzy_score(path, query_buffer);
            },
            20);
        auto tok = std::chrono::steady_clock::now();
        printf("Ranked %ld paths in %ldms", g_indexed_paths.size(),
               std::chrono::duration_cast<std::chrono::milliseconds>(tok - tik)
                   .count());
    };

    while (running) {
        XNextEvent(display, &event);

        switch (event.type) {
        case Expose:
            if (event.xexpose.count == 0) {
                rerank();
                draw(display, window, WIDTH, TOTAL_HEIGHT, query_buffer,
                     current_results, selected_index);
            }
            break;

        case KeyPress: {
            const KeySym keysym = XLookupKeysym(&event.xkey, 0);

            if (keysym == XK_Escape) {
                running = false;
            } else if (keysym == XK_Up) {
                // Move selection up
                if (!current_results.empty() && selected_index > 0) {
                    selected_index--;
                    needs_redraw = true;
                }
                printf("Selected index: %d\n", selected_index);
            } else if (keysym == XK_Down) {
                // Move selection down
                const int max_index =
                    std::min(static_cast<int>(current_results.size()) - 1,
                             MAX_VISIBLE_OPTIONS - 1);
                if (!current_results.empty() && selected_index < max_index) {
                    selected_index++;
                    needs_redraw = true;
                }
                printf("Selected index: %d\n", selected_index);
            } else if (keysym == XK_Return) {
                // Handle Enter key - for now just print selection
                if (!current_results.empty() &&
                    std::cmp_less(selected_index, current_results.size())) {
                    printf("Selected: %s\n",
                           g_indexed_paths
                               .at(current_results[selected_index].index)
                               .data());
                    running = false; // Exit for now
                }
            } else if (keysym == XK_BackSpace) {
                // Handle backspace
                if (!query_buffer.empty()) {
                    query_buffer.pop_back();
                    rerank();
                    selected_index = 0; // Reset selection when search changes
                    needs_redraw = true;
                }
            } else {
                // Handle regular character input
                char char_buffer[32];
                const int len =
                    XLookupString(&event.xkey, char_buffer, sizeof(char_buffer),
                                  nullptr, nullptr);
                if (len > 0) {
                    char_buffer[len] = '\0';
                    // Only add printable characters
                    for (int i = 0; i < len; ++i) {
                        if (char_buffer[i] >= 32 && char_buffer[i] < 127) {
                            query_buffer += char_buffer[i];
                            rerank();
                            selected_index =
                                0; // Reset selection when search changes
                            needs_redraw = true;
                        }
                    }
                    printf("Search buffer: \"%s\" (%zu results)\n",
                           query_buffer.c_str(), current_results.size());
                }
            }

            // Redraw if anything changed
            if (needs_redraw) {
                draw(display, window, WIDTH, TOTAL_HEIGHT, query_buffer,
                     current_results, selected_index);
                needs_redraw = false;
            }
            break;
        }
        }
    }

    return 0;
}