#include "actions.h"
#include "config.h"
#include "fuzzy.h"
#include "indexer.h"
#include "lastwriterwinsslot.h"
#include "logger.h"
#include "ranker.h"
#include "streamingindex.h"
#include "ui.h"
#include "utility.h"
#include "window.h"

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
    // Initialize logger first
    Logger::getInstance().init();
    LOG_INFO("Khala launcher starting up");
    
    const Config config = Config::load(Config::default_path());
    const defer save_config([config]() noexcept {
        try {
            config.save(config.config_path);
        } catch (const std::exception &e) {
            LOG_ERROR("Could not write config to %s: %s",
                   config.config_path.c_str(), e.what());
        }
    });
    const auto global_actions = get_global_actions(config);

    PlatformWindow window(
        ui::RelScreenCoord{
            .x = config.x_position,
            .y = config.y_position,
        },
        ui::RelScreenCoord{
            .x = config.width_ratio,
            .y = config.height_ratio,
        });

    const auto max_window_height =
        static_cast<unsigned int>(window.get_screen_height() * config.height_ratio);
    const size_t max_visible_items =
        ui::calculate_max_visible_items(max_window_height, config.font_size);

    ui::State state;

    // Shared state
    StreamingIndex streaming_index;
    std::vector<indexer::DesktopApp> desktop_apps =
        indexer::scan_desktop_files();
    LOG_INFO("Loaded %zu desktop apps", desktop_apps.size());

    // Communication channels
    LastWriterWinsSlot<ResultUpdate> result_updates;
    std::atomic<RankerMode> ranker_mode{RankerMode::FileSearch};

    // Ranker request state - GUI writes, ranker reads
    RankerRequest ranker_request = {
        "", ui::required_item_count(state, max_visible_items)};
    std::mutex query_mutex;
    std::atomic_bool query_changed{true}; // Signal initial processing
    std::atomic_bool should_exit{false};

    LOG_INFO("Loading index for %s...",
           fs::canonical(config.index_root).generic_string().c_str());

    // Launch streaming indexer
    auto index_future = std::async(std::launch::async, [&]() {
        indexer::scan_filesystem_streaming(config.index_root, streaming_index,
                                           config.ignore_dirs,
                                           config.ignore_dir_names, 10'000);
        LOG_INFO("Scan complete - %zu total files",
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
    bool redraw = true;
    while (true) {
        const std::vector<ui::UserInputEvent> input_events =
            window.get_input_events(false);

        // Process input events and generate high-level events
        std::vector<ui::Event> events;
        for (const auto &input_event : input_events) {
            auto handled_events =
                ui::handle_user_input(state, input_event, config);
            events.insert(events.end(), handled_events.begin(),
                          handled_events.end());
        }

        // Small sleep during non-blocking mode to avoid busy looping
        if (events.empty()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(16)); // ~60 FPS
        }

        // Process high-level events
        bool exit_requested = false;
        for (const auto &event : events) {
            redraw = true;
            if (std::holds_alternative<ui::ExitRequested>(event)) {
                exit_requested = true;
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
                LOG_DEBUG("Selected: %s",
                       state.get_selected_item().title.c_str());
                state.set_error(process_command(state.get_selected_item().command, config));
                if (!state.has_error() && config.quit_on_action) {
                    should_exit = true;
                    break;
                }
            } else if (std::holds_alternative<ui::ContextMenuToggled>(event)) {
                // Restore file search results when toggling back from context menu
                if (std::holds_alternative<ui::FileSearch>(state.mode) &&
                    state.cached_file_search_update.has_value()) {

                    const auto& cached = *state.cached_file_search_update;

                    // Restore items (re-process from cached FileResults)
                    state.items = ui::convert_file_results_to_items(cached.results);
                    state.selected_item_index = 0;
                    state.visible_range_offset = 0;
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
                            .path = std::nullopt,
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
        if (exit_requested) {
            break;
        }

        // Process streaming result updates
        ResultUpdate update;
        if (result_updates.try_read(update)) {
            if (std::holds_alternative<ui::FileSearch>(state.mode)) {
                // Cache the update for quick restoration from ContextMenu
                state.cached_file_search_update = update;

                current_file_results = std::move(update.results);

                // Convert results to UI items (keep all ranked results for scrolling)
                state.items = ui::convert_file_results_to_items(current_file_results);
                redraw = true;
            }
        }

        // Render UI
        if (redraw) {
            // Calculate and apply window resize before getting context
            const auto max_height =
                static_cast<unsigned int>(window.get_screen_height() * config.height_ratio);
            const size_t current_max_visible_items =
                ui::calculate_max_visible_items(max_height, config.font_size);
            const unsigned int new_height = ui::calculate_window_height(
                config.font_size, state.items.size(), current_max_visible_items);

            if (new_height != static_cast<unsigned int>(window.get_height())) {
                window.resize(new_height, static_cast<unsigned int>(window.get_width()));
            }
            try {
                // Get context and draw
                cairo_t *cr = window.get_cairo_context();
                ui::draw(cr, window.get_width(), window.get_height(), config, state);
                window.commit_surface();
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to render UI: %s", e.what());
                // Continue running - don't crash on render failure
            }
            redraw = false;
        }
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