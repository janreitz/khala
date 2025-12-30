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
static constexpr size_t MAX_VISIBLE_OPTIONS = 8;
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

    // Find ARGB visual for transparency support
    XVisualInfo vinfo;
    XMatchVisualInfo(display, screen, 32, TrueColor, &vinfo);

    // Create colormap for ARGB visual
    const Colormap colormap = XCreateColormap(display, RootWindow(display, screen), vinfo.visual, AllocNone);
    const defer cleanup_colormap([display, colormap]() noexcept { XFreeColormap(display, colormap); });

    // Create window attributes
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.colormap = colormap;
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask;

    // Create the window with ARGB visual for transparency
    const Window window = XCreateWindow(
        display, RootWindow(display, screen), x, y, WIDTH, TOTAL_HEIGHT, 0,
        vinfo.depth, InputOutput, vinfo.visual,
        CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel | CWEventMask, &attrs);

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
    std::string input_buffer;
    size_t action_index = 0;

    std::vector<ui::Action> display_results;

    bool first_iteration = true;

    while (true) {
        const size_t max_action_index =
            std::min(display_results.size() - 1, MAX_VISIBLE_OPTIONS - 1);
        ui::UserInput input = ui::process_input_events(
            display, input_buffer, action_index, max_action_index);

        if (input.exit_requested) {
            break;
        }

        if (input.action_requested) {
            printf("Selected: %s\n", current_matches.at(action_index).data());
        }

        if (input.input_buffer_changed) {
            {
                std::lock_guard lock(query_mutex);
                query_buffer = input_buffer;
            }
            query_changed.store(true, std::memory_order_release);
            query_changed.notify_one();
        }

        // Check for new results
        bool new_results_available = false;
        if (results_ready.exchange(false, std::memory_order_acquire)) {
            std::lock_guard lock(results_mutex);
            display_results.clear();
            display_results.reserve(current_matches.size());
            const size_t visible_action_count =
                std::min(current_matches.size(), MAX_VISIBLE_OPTIONS);
            for (size_t i = 0; i < visible_action_count; i++) {
                display_results.push_back(ui::Action{
                    .title = current_matches.at(i).data(),
                    .description = "",
                });
            }
            new_results_available = true;
        }

        bool needs_redraw = input.selected_action_index_changed ||
                            input.input_buffer_changed || new_results_available || first_iteration;

        if (needs_redraw) {
            ui::draw(display, window, WIDTH, TOTAL_HEIGHT, INPUT_HEIGHT,
                     query_buffer, OPTION_HEIGHT, display_results,
                     action_index);
        }

        first_iteration = false;
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