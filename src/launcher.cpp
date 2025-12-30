#include "fuzzy.h"
#include "indexer.h"
#include "ui.h"
#include "utility.h"

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <utility>

namespace fs = std::filesystem;

static constexpr int WIDTH = 600;
static constexpr int INPUT_HEIGHT = 40;
static constexpr int OPTION_HEIGHT = 30;
static constexpr int MAX_VISIBLE_OPTIONS = 8;
static constexpr int DROPDOWN_HEIGHT = MAX_VISIBLE_OPTIONS * OPTION_HEIGHT;
static constexpr int TOTAL_HEIGHT = INPUT_HEIGHT + DROPDOWN_HEIGHT;

int main(int argc, char *argv[])
{
    // Shared state
    PackedStrings indexed_paths;
    std::atomic_bool index_loaded{false};

    // Results shared between ranking thread and GUI
    PackedStrings current_matches;
    std::mutex results_mutex;
    std::atomic_bool results_ready{false};

    // Query state - GUI writes, ranker reads
    std::string query_buffer;
    std::mutex query_mutex;
    std::atomic_bool query_changed{false};
    std::atomic_bool should_exit{false};

    const fs::path root_path =
        (argc > 1) ? argv[1] : fs::path(std::getenv("HOME"));

    printf("Loading index for %s...\n", root_path.c_str());

    // Launch indexing thread
    auto index_future = std::async(
        std::launch::async, [&indexed_paths, &index_loaded, &root_path]() {
            indexed_paths = indexer::scan_filesystem_parallel(root_path);
            index_loaded.store(true, std::memory_order_release);
            index_loaded.notify_all();
            printf("Loaded %zu files\n", indexed_paths.size());
        });

    // Launch ranking thread
    auto rank_future = std::async(std::launch::async, [&]() {
        // Wait for index to be ready
        index_loaded.wait(false);

        while (!should_exit.load(std::memory_order_relaxed)) {
            // Wait for query change or exit
            query_changed.wait(false);
            if (should_exit.load(std::memory_order_relaxed))
                break;
            query_changed.store(false, std::memory_order_relaxed);

            // Get current query
            std::string query;
            {
                std::lock_guard lock(query_mutex);
                query = query_buffer;
            }
            auto tik = std::chrono::steady_clock::now();

            // Do ranking (expensive, outside lock)
            auto new_ranks = rank_parallel(
                indexed_paths,
                [&query](std::string_view path) {
                    return fuzzy::fuzzy_score(path, query);
                },
                20);

            auto tok = std::chrono::steady_clock::now();
            printf(
                "Ranked %ld paths in %ldms", indexed_paths.size(),
                std::chrono::duration_cast<std::chrono::milliseconds>(tok - tik)
                    .count());

            PackedStrings new_results;
            for (const auto &rank : new_ranks) {
                new_results.push(indexed_paths.at(rank.index).data());
            }

            // Publish results
            {
                std::lock_guard lock(results_mutex);
                current_matches = std::move(new_results);
            }
            results_ready.store(true, std::memory_order_release);
            results_ready.notify_one();
        }
    });

    printf("Loaded %zu files\n", indexed_paths.size());

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
    int selected_index = 0;
    bool needs_redraw = true;

    std::string query_input;
    bool query_input_changed = false;
    PackedStrings display_results;

    while (running) {
        while (XPending(display) > 0) {
            XNextEvent(display, &event);

            if (event.type == Expose) {
                if (event.xexpose.count == 0) {
                    needs_redraw = true;
                }
            } else if (event.type == KeyPress) {
                const KeySym keysym = XLookupKeysym(&event.xkey, 0);

                if (keysym == XK_Escape) {
                    running = false;
                } else if (keysym == XK_Up) {
                    // Move selection up
                    if (!current_matches.empty() && selected_index > 0) {
                        selected_index--;
                        needs_redraw = true;
                    }
                    printf("Selected index: %d\n", selected_index);
                } else if (keysym == XK_Down) {
                    // Move selection down
                    const int max_index =
                        std::min(static_cast<int>(current_matches.size()) - 1,
                                 MAX_VISIBLE_OPTIONS - 1);
                    if (!current_matches.empty() &&
                        selected_index < max_index) {
                        selected_index++;
                        needs_redraw = true;
                    }
                    printf("Selected index: %d\n", selected_index);
                } else if (keysym == XK_Return) {
                    // Handle Enter key - for now just print selection
                    if (!current_matches.empty() &&
                        std::cmp_less(selected_index, current_matches.size())) {
                        printf("Selected: %s\n",
                               current_matches.at(selected_index).data());
                        running = false; // Exit for now
                    }
                } else if (keysym == XK_BackSpace) {
                    // Handle backspace
                    if (!query_input.empty()) {
                        query_input.pop_back();
                        query_input_changed = true;
                        selected_index =
                            0; // Reset selection when search changes
                        needs_redraw = true;
                    }
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
                                query_input += char_buffer[i];
                                query_input_changed = true;
                                selected_index =
                                    0; // Reset selection when search changes
                                needs_redraw = true;
                            }
                        }
                        printf("Search buffer: \"%s\" (%zu results)\n",
                               query_buffer.c_str(), current_matches.size());
                    }
                }
                break;
            }
        }

        if (query_input_changed) {
            {
                std::lock_guard lock(query_mutex);
                query_buffer = query_input;
            }
            query_changed.store(true, std::memory_order_release);
            query_changed.notify_one();
        }

        // Check for new results
        if (results_ready.exchange(false, std::memory_order_acquire)) {
            std::lock_guard lock(results_mutex);
            display_results = current_matches; // copy for GUI
            needs_redraw = true;
        }

        if (needs_redraw) {
            ui::draw(display, window, WIDTH, TOTAL_HEIGHT, INPUT_HEIGHT,
                     OPTION_HEIGHT, MAX_VISIBLE_OPTIONS, query_buffer,
                     current_matches, selected_index);
            needs_redraw = false;
        }
    }

    // Signal threads to exit
    should_exit.store(true, std::memory_order_release);
    query_changed.store(true);
    query_changed.notify_one();

    // Wait for threads
    index_future.wait();
    rank_future.wait();

    return 0;
}