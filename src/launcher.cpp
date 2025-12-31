#include "actions.h"
#include "config.h"
#include "fuzzy.h"
#include "indexer.h"
#include "ui.h"
#include "utility.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <mutex>
#include <ranges>
#include <string>
#include <utility>

namespace fs = std::filesystem;

int main()
{
    const Config config = Config::load(Config::default_path());

    // Shared state
    PackedStrings indexed_paths;
    std::vector<indexer::DesktopApp> desktop_apps =
        indexer::scan_desktop_files();
    printf("Loaded %zu desktop apps\n", desktop_apps.size());
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

    printf("Loading index for %s...\n", fs::canonical(config.index_root).generic_string().c_str());

    // Launch indexing thread
    auto index_future = std::async(
        std::launch::async, [&indexed_paths, &index_loaded, &config]() {
            indexed_paths = indexer::scan_filesystem_parallel(config.index_root, config.ignore_dirs);
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

    ui::XWindow window(config);
    ui::State state;
    bool first_iteration = true;
    const auto global_actions = get_global_actions(config);

    while (true) {
        const auto event = ui::process_input_events(window.display, state);

        if (event == ui::Event::NoEvent) {
        } else if (event == ui::Event::ExitRequested) {
            break;
        } else if (event == ui::Event::ActionRequested) {
            printf("Selected: %s\n",
                   current_matches.at(state.selected_item_index).data());
            process_command(state.get_selected_action().command, config);
            if (config.quit_on_action) {
                break;
            }
        } else if (event == ui::Event::InputChanged) {
            state.selected_item_index =
                0; // Reset selection when search changes

            // Check if we're in command mode (input starts with '>') or app
            // mode (starts with '!')
            const bool command_mode =
                !state.input_buffer.empty() && state.input_buffer[0] == '>';
            const bool app_mode =
                !state.input_buffer.empty() && state.input_buffer[0] == '!';

            if (command_mode) {
                // Command palette mode - search utility commands
                const std::string query =
                    state.input_buffer.substr(1); // Strip '>'
                state.current_query = query;

                auto to_item = [&global_actions](const RankResult &r) {
                    const auto &action = global_actions[r.index];
                    return ui::Item{
                        .title = action.title,
                        .description = action.description,
                        .actions = {action},
                    };
                };

                auto ranked = rank(
                    global_actions,
                    [&query](const Action &action) {
                        return fuzzy::fuzzy_score(action.title, query);
                    },
                    config.max_visible_items);

                auto transformed = ranked | std::views::transform(to_item);
                state.items.assign(transformed.begin(), transformed.end());
            } else if (app_mode) {
                // App search mode - search desktop applications only
                const std::string query =
                    state.input_buffer.substr(1); // Strip '!'
                state.current_query = query;

                auto to_item =
                    [](const RankResult &r,
                       const std::vector<indexer::DesktopApp> &apps) {
                        const auto &app = apps[r.index];
                        return ui::Item{
                            .title = app.name,
                            .description = app.description,
                            .actions = {Action{
                                .title = "Launch " + app.name,
                                .description = "Launch this application",
                                .command =
                                    CustomCommand{.path = std::nullopt,
                                                  .shell_cmd =
                                                      app.exec_command}}},
                        };
                    };

                auto ranked = rank(
                    desktop_apps,
                    [&query](const indexer::DesktopApp &app) {
                        return fuzzy::fuzzy_score(app.name, query);
                    },
                    config.max_visible_items);

                state.items.clear();
                for (const auto &r : ranked) {
                    state.items.push_back(to_item(r, desktop_apps));
                }
            } else {
                // File search mode
                state.current_query = state.input_buffer;
                {
                    std::lock_guard lock(query_mutex);
                    query_buffer = state.input_buffer;
                }
                query_changed.store(true, std::memory_order_release);
                query_changed.notify_one();
            }
        }

        // Check for new results (file search mode only)
        bool new_results_available = false;
        const bool command_mode =
            !state.input_buffer.empty() && state.input_buffer[0] == '>';
        const bool app_mode =
            !state.input_buffer.empty() && state.input_buffer[0] == '!';
        if (!command_mode && !app_mode &&
            results_ready.exchange(false, std::memory_order_acquire)) {
            std::lock_guard lock(results_mutex);
            state.items.clear();
            state.items.reserve(current_matches.size());
            const size_t visible_action_count =
                std::min(current_matches.size(), config.max_visible_items);
            for (size_t i = 0; i < visible_action_count; i++) {
                fs::path path(current_matches.at(i).data());
                state.items.push_back(ui::Item{
                    .title = path.filename(),
                    .description = path.parent_path(),
                    .actions = make_file_actions(path, config),
                });
            }
            new_results_available = true;
        }

        const bool needs_redraw = event != ui::Event::NoEvent ||
                                  new_results_available || first_iteration;

        if (needs_redraw) {
            ui::draw(window, config, state);
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

    config.save(config.config_path);

    return 0;
}