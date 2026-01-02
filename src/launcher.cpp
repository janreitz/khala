#include "actions.h"
#include "config.h"
#include "fuzzy.h"
#include "indexer.h"
#include "lastwriterwinsslot.h"
#include "ranker.h"
#include "streamingindex.h"
#include "ui.h"
#include "utility.h"
#include "xwindow.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

int main()
{
    const Config config = Config::load(Config::default_path());
    const defer save_config([config]() noexcept {
        try {
            config.save(config.config_path);
        } catch (const std::exception &e) {
            printf("Could not write config to %s: %s",
                   config.config_path.c_str(), e.what());
        }
    });
    const auto global_actions = get_global_actions(config);

    XWindow window(
        ui::RelScreenCoord{
            .x = config.x_position,
            .y = config.y_position,
        },
        ui::RelScreenCoord{
            .x = config.width_ratio,
            .y = config.height_ratio,
        });

    const int max_window_height =
        static_cast<int>(window.screen_height * config.height_ratio);
    const size_t max_visible_items =
        ui::calculate_max_visible_items(max_window_height, config.font_size);

    // Shared state
    StreamingIndex streaming_index;
    std::vector<indexer::DesktopApp> desktop_apps =
        indexer::scan_desktop_files();
    printf("Loaded %zu desktop apps\n", desktop_apps.size());

    // Communication channels
    LastWriterWinsSlot<ResultUpdate> result_updates;
    std::atomic<RankerMode> ranker_mode{RankerMode::FileSearch};

    // Query state - GUI writes, ranker reads
    std::string query_buffer = ""; // Start with empty query
    std::mutex query_mutex;
    std::atomic_bool query_changed{true}; // Signal initial processing
    std::atomic_bool should_exit{false};

    printf("Loading index for %s...\n",
           fs::canonical(config.index_root).generic_string().c_str());

    // Launch streaming indexer
    auto index_future = std::async(std::launch::async, [&]() {
        indexer::scan_filesystem_streaming(config.index_root, streaming_index,
                                           config.ignore_dirs,
                                           config.ignore_dir_names, 10'000);
        printf("Scan complete - %zu total files\n",
               streaming_index.get_total_files());
    });

    // Launch progressive ranking worker
    auto rank_future = std::async(std::launch::async, [&]() {
        size_t processed_chunks = 0;
        std::vector<FileResult> accumulated_results;
        std::string current_query;

        while (!should_exit.load(std::memory_order_relaxed)) {
            auto mode = ranker_mode.load(std::memory_order_acquire);

            if (mode == RankerMode::Inactive) {
                // Sleep when not in file search mode
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            if (mode == RankerMode::Paused) {
                // Reset state when query is changing
                processed_chunks = 0;
                accumulated_results.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // Check for query changes
            if (query_changed.exchange(false, std::memory_order_acq_rel)) {
                std::lock_guard lock(query_mutex);
                if (current_query != query_buffer) {
                    current_query = query_buffer;
                    processed_chunks = 0;
                    accumulated_results.clear();
                }
            }

            // Wait for new chunks or scan completion
            const auto available = streaming_index.get_available_chunks();
            if (processed_chunks >= available &&
                !streaming_index.is_scan_complete()) {
                streaming_index.wait_for_chunks(processed_chunks + 1);
                continue;
            }

            // Process available chunks
            while (processed_chunks < streaming_index.get_available_chunks()) {
                const auto chunk = streaming_index.get_chunk(processed_chunks);
                if (!chunk) {
                    break;
                }

                // Rank this chunk
                auto chunk_results = rank_parallel(
                    *chunk,
                    [&current_query](std::string_view path) {
                        return fuzzy::fuzzy_score(path, current_query);
                    }, max_visible_items);

                // Convert RankResult to FileResult with actual paths
                std::vector<FileResult> file_results;
                file_results.reserve(chunk_results.size());
                for (const auto &rank : chunk_results) {
                    file_results.push_back(
                        FileResult{.path = std::string(chunk->at(rank.index)),
                                   .score = rank.score});
                }

                // Merge with accumulated results
                accumulated_results =
                    merge_top_results(accumulated_results, file_results, max_visible_items);

                ++processed_chunks;

                // Send progressive update to UI
                ResultUpdate update;
                update.results = accumulated_results;
                update.scan_complete = streaming_index.is_scan_complete();
                update.total_files = streaming_index.get_total_files();
                update.processed_chunks = processed_chunks;

                result_updates.write(std::move(update));
            }

            // Final update when scan completes
            if (streaming_index.is_scan_complete() &&
                processed_chunks == streaming_index.get_available_chunks()) {

                ResultUpdate final_update;
                final_update.results = accumulated_results;
                final_update.scan_complete = true;
                final_update.total_files = streaming_index.get_total_files();
                final_update.processed_chunks = processed_chunks;

                result_updates.write(std::move(final_update));

                // Wait for next query or mode change
                while (ranker_mode.load(std::memory_order_acquire) ==
                           RankerMode::FileSearch &&
                       !query_changed.load(std::memory_order_acquire) &&
                       !should_exit.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        }
    });

    // Current file search state
    std::vector<FileResult> current_file_results;
    ui::State state;
    while (true) {
        // Use non-blocking mode when actively scanning to allow UI updates
        const bool should_block =
            !(std::holds_alternative<ui::FileSearch>(state.mode) &&
              !state.scan_complete);
        const std::vector<ui::UserInputEvent> input_events =
            get_input_events(window.display, should_block);

        // Process input events and generate high-level events
        std::vector<ui::Event> events;
        for (const auto &input_event : input_events) {
            auto handled_events =
                ui::handle_user_input(state, input_event, config);
            events.insert(events.end(), handled_events.begin(),
                          handled_events.end());
        }

        // Small sleep during non-blocking mode to avoid busy looping
        if (!should_block && events.empty()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(16)); // ~60 FPS
        }

        // Process high-level events
        bool should_exit = false;
        for (const auto &event : events) {
            if (std::holds_alternative<ui::ExitRequested>(event)) {
                should_exit = true;
                break;
            } else if (std::holds_alternative<ui::ActionRequested>(event)) {
                if (std::holds_alternative<ui::FileSearch>(state.mode) &&
                    !current_file_results.empty() &&
                    state.selected_item_index < current_file_results.size()) {
                    // For file search, we have the actual path stored in the
                    // result
                    const auto &result =
                        current_file_results[state.selected_item_index];
                    printf("Selected: %s\n", result.path.c_str());

                    ui::Item item;
                    item.title = result.path;
                    item.command = CustomCommand{.path = fs::path(result.path),
                                                 .shell_cmd = ""};
                    process_command(item.command, config);
                } else {
                    printf("Selected: %s\n",
                           state.get_selected_item().title.c_str());
                    process_command(state.get_selected_item().command, config);
                }

                if (config.quit_on_action) {
                    should_exit = true;
                    break;
                }
            } else if (std::holds_alternative<ui::InputChanged>(event)) {
                state.selected_item_index =
                    0; // Reset selection when search changes

                // Command palette mode - search utility commands
                if (!state.input_buffer.empty() &&
                    state.input_buffer[0] == '>') {
                    ranker_mode.store(RankerMode::Inactive,
                                      std::memory_order_release);

                    const auto query = state.input_buffer.substr(1);
                    state.mode = ui::CommandSearch{.query = query};

                    auto ranked = rank(
                        global_actions,
                        [&query](const ui::Item &item) {
                            return fuzzy::fuzzy_score(item.title, query);
                        },
                        max_visible_items);

                    state.items.clear();
                    for (const auto &r : ranked) {
                        state.items.push_back(global_actions[r.index]);
                    }
                    // App search mode - search desktop applications only
                } else if (!state.input_buffer.empty() &&
                           state.input_buffer[0] == '!') {
                    ranker_mode.store(RankerMode::Inactive,
                                      std::memory_order_release);

                    const auto query = state.input_buffer.substr(1);
                    state.mode = ui::AppSearch{.query = query};

                    auto ranked = rank(
                        desktop_apps,
                        [&query](const indexer::DesktopApp &app) {
                            return fuzzy::fuzzy_score(app.name, query);
                        },
                        max_visible_items);

                    state.items.clear();
                    for (const auto &r : ranked) {
                        const auto &app = desktop_apps[r.index];
                        state.items.push_back(ui::Item{
                            .title = app.name,
                            .description = app.description,
                            .command =
                                CustomCommand{.path = std::nullopt,
                                              .shell_cmd = app.exec_command},
                        });
                    }
                } else {
                    // File search mode - activate streaming ranker
                    state.mode = ui::FileSearch{.query = state.input_buffer};

                    {
                        std::lock_guard lock(query_mutex);
                        query_buffer = state.input_buffer;
                    }

                    query_changed.store(true, std::memory_order_release);
                    query_changed.notify_one();
                    ranker_mode.store(RankerMode::FileSearch,
                                      std::memory_order_release);
                }
            }
        }

        // Exit if requested
        if (should_exit) {
            break;
        }

        // Process streaming result updates
        ResultUpdate update;
        if (result_updates.try_read(update)) {
            if (std::holds_alternative<ui::FileSearch>(state.mode)) {
                current_file_results = std::move(update.results);

                // Update progress tracking
                state.scan_complete = update.scan_complete;
                state.total_files = update.total_files;
                state.processed_chunks = update.processed_chunks;

                // Convert results to UI items
                const auto item_count =
                    std::min(current_file_results.size(), max_visible_items);
                state.items.clear();
                state.items.reserve(item_count);

                for (size_t i = 0; i < item_count; ++i) {
                    const auto &result = current_file_results[i];
                    state.items.push_back(ui::Item{
                        // Display actual file paths from results
                        .title = fs::canonical(result.path).generic_string(),
                        .description = serialize_file_info(result.path),
                        .command = CustomCommand{.path = fs::path(result.path),
                                                 .shell_cmd = ""},
                    });
                }
            }
        }

        // Render UI
        ui::draw(window, config, state);
    }

    // Cleanup
    should_exit.store(true, std::memory_order_release);
    query_changed.notify_all();
    ranker_mode.store(RankerMode::Inactive, std::memory_order_release);

    if (index_future.valid())
        index_future.wait();
    if (rank_future.valid())
        rank_future.wait();
    return 0;
}