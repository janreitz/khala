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

    ui::State state;

    // Shared state
    StreamingIndex streaming_index;
    std::vector<indexer::DesktopApp> desktop_apps =
        indexer::scan_desktop_files();
    printf("Loaded %zu desktop apps\n", desktop_apps.size());

    // Communication channels
    LastWriterWinsSlot<ResultUpdate> result_updates;
    std::atomic<RankerMode> ranker_mode{RankerMode::FileSearch};

    // Ranker request state - GUI writes, ranker reads
    RankerRequest ranker_request = {
        "", ui::required_item_count(state, max_visible_items)};
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
    StreamingRanker ranker(streaming_index, result_updates, ranker_mode,
                           ranker_request, query_mutex, query_changed,
                           should_exit);

    auto rank_future =
        std::async(std::launch::async, [&ranker]() { ranker.run(); });

    // Current file search state
    std::vector<FileResult> current_file_results;
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
            } else if (std::holds_alternative<ui::SelectionChanged>(event)) {
                // Adjust visible range to keep selected item visible
                const bool adjusted =
                    ui::adjust_visible_range(state, max_visible_items);
                if (adjusted) {
                    const auto required_item_count =
                        ui::required_item_count(state, max_visible_items);
                    std::lock_guard lock(query_mutex);
                    if (required_item_count > ranker_request.requested_count) {
                        ranker_request.requested_count = required_item_count;
                        query_changed.store(true, std::memory_order_release);
                        query_changed.notify_one();
                    }
                }
            } else if (std::holds_alternative<ui::ActionRequested>(event)) {
                printf("Selected: %s\n",
                       state.get_selected_item().title.c_str());
                auto error = process_command(state.get_selected_item().command, config);
                }
            } else if (std::holds_alternative<ui::InputChanged>(event)) {
                state.selected_item_index =
                    0; // Reset selection when search changes
                state.visible_range_offset = 0; // Reset scroll position

                // Command palette mode - search utility commands
                if (!state.input_buffer.empty() &&
                    state.input_buffer[0] == '>') {
                    ranker_mode.store(RankerMode::Inactive,
                                      std::memory_order_release);

                    const auto query = state.input_buffer.substr(1);
                    state.mode = ui::CommandSearch{.query = query};

                    // For command search, rank all (usually small dataset)
                    auto ranked = rank(
                        global_actions,
                        [&query](const ui::Item &item) {
                            return fuzzy::fuzzy_score(item.title, query);
                        },
                        global_actions.size());

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

                    // For app search, rank all (usually small dataset)
                    auto ranked = rank(
                        desktop_apps,
                        [&query](const indexer::DesktopApp &app) {
                            return fuzzy::fuzzy_score(app.name, query);
                        },
                        desktop_apps.size());

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
                        ranker_request.query = to_lower(state.input_buffer);
                        ranker_request.requested_count =
                            ui::required_item_count(state, max_visible_items);
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
                state.total_available_results = update.total_available_results;

                // Convert results to UI items (keep all ranked results for
                // scrolling)
                state.items.clear();
                state.items.reserve(current_file_results.size());

                for (size_t i = 0; i < current_file_results.size(); ++i) {
                    const auto &result = current_file_results[i];

                    try {
                        const auto file_path = fs::canonical(result.path);

                        if (fs::is_directory(file_path)) {
                            state.items.push_back(ui::Item{
                                .title = "📁 " + file_path.generic_string(),
                                .description = serialize_file_info(file_path),
                                .command = OpenDirectory{.path = file_path},
                            });
                        } else {
                            state.items.push_back(ui::Item{
                                .title = "📄 " + file_path.generic_string(),
                                .description = serialize_file_info(file_path),
                                .command = OpenFile{.path = file_path},
                            });
                        }
                    } catch (const std::exception& e) {
                        printf("Could not make canonical path for %s: %s",
                               result.path.c_str(), e.what());
                    }
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