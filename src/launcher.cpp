#include "actions.h"
#include "config.h"
#include "fuzzy.h"
#include "indexer.h"
#include "lastwriterwinsslot.h"
#include "logger.h"
#include "ranker.h"
#include "streamingindex.h"
#include "types.h"
#include "ui.h"
#include "utility.h"
#include "window.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <future>
#include <string>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

// Constants for event loop timing and indexing
constexpr size_t INDEXER_BATCH_SIZE = 10'000;
constexpr int EVENT_LOOP_SLEEP_MS = 16; // ~60 FPS

int main()
{
    // Initialize logger first
    Logger::getInstance().init(platform::get_data_dir() / "logs");
    LOG_INFO("Khala launcher starting up");

    ui::State state;
    load_history(state.file_search_history);
    const defer save_hist([&state]() noexcept {
        try {
            save_history(state.file_search_history);
        } catch (...) {
        }
    });
    auto [config, config_warnings] = load_config(Config::default_path());
    const defer save_config([&config]() noexcept {
        try {
            config.save(config.config_path);
        } catch (const std::exception &e) {
            LOG_ERROR("Could not write config to %s: %s",
                      config.config_path.c_str(), e.what());
        }
    });
    for (const auto &warning : config_warnings) {
        state.push_error(warning);
    }
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

    // Background mode setup
    if (config.background_mode) {
        window.hide();
        LOG_INFO("Background mode enabled, window hidden");

        if (window.register_global_hotkey(config.hotkey)) {
            LOG_INFO("Registered global hotkey: %s",
                     to_string(config.hotkey).c_str());
            state.background_mode_active = true;
        } else {
            LOG_WARNING("Failed to register global hotkey: %s - disabling "
                        "background mode",
                        to_string(config.hotkey).c_str());
            LOG_WARNING(
                "The hotkey may already be in use by another application");
            window.show();
        }
    }

    const auto max_window_height = static_cast<unsigned int>(
        window.get_screen_height() * config.height_ratio);
    const size_t max_visible_items =
        ui::calculate_max_visible_items(max_window_height, config.font_size);

    // Shared state
    StreamingIndex streaming_index;
    std::vector<ApplicationInfo> desktop_apps = platform::scan_app_infos();
    LOG_INFO("Loaded %zu desktop apps", desktop_apps.size());

    // Communication channels
    LastWriterWinsSlot<ResultUpdate> result_updates;

    LOG_INFO("Loading index for %zu root(s)...", config.index_roots.size());

    // Launch streaming indexer
    auto index_future = std::async(std::launch::async, [&]() {
        indexer::scan_filesystem_streaming(
            config.index_roots, streaming_index, config.ignore_dirs,
            config.ignore_dir_names, INDEXER_BATCH_SIZE);
        LOG_INFO("Scan complete - %zu total files",
                 streaming_index.get_total_files());
    });

    // Launch progressive ranking worker
    StreamingRanker ranker(streaming_index, result_updates);
    ranker.update_query("");
    ranker.update_requested_count(
        ui::required_item_count(state, max_visible_items));

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
            // Longer sleep when window is hidden in background mode
            if (state.background_mode_active && !window.is_visible()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(EVENT_LOOP_SLEEP_MS));
            }
        }

        // Process high-level events
        std::vector<Effect> effects;
        for (const auto &event : events) {
            redraw = true;
            std::visit(
                overloaded{
                    [&state, &effects, &window](ui::VisibilityToggleRequested) {
                        if (!state.background_mode_active) {
                            return;
                        }
                        if (window.is_visible()) {
                            effects.push_back(HideWindow{});
                        } else {
                            window.show();
                            LOG_DEBUG("Window shown via hotkey");
                        }
                    },
                    [&effects](ui::ExitRequested) {
                        effects.push_back(QuitApplication{});
                    },
                    [&state, &ranker, max_visible_items](ui::SelectionChanged) {
                        // Adjust visible range to keep selected item visible
                        const bool adjusted =
                            ui::adjust_visible_range(state, max_visible_items);
                        if (adjusted) {
                            const auto required_item_count =
                                ui::required_item_count(state,
                                                        max_visible_items);
                            ranker.update_requested_count(required_item_count);
                        }
                    },
                    [&state, &ranker, max_visible_items](ui::ViewportChanged) {
                        // Viewport was scrolled, update required item count
                        const auto required_item_count =
                            ui::required_item_count(state, max_visible_items);
                        ranker.update_requested_count(required_item_count);
                    },
                    [&state, &config,
                     &effects](const ui::ActionRequested &req) {
                        if (std::holds_alternative<ui::FileSearch>(
                                state.mode) &&
                            !state.input_buffer.empty()) {
                            state.file_search_history.push(state.input_buffer);
                        }
                        const auto cmd_result =
                            process_command(req.command, config);
                        if (!cmd_result.has_value()) {
                            state.push_error(cmd_result.error());
                            return;
                        }
                        if (cmd_result->has_value()) {
                            // Command returned an effect - process it
                            effects.push_back(*cmd_result.value());
                        } else if (config.quit_on_action) {
                            // External action completed - apply quit_on_action
                            if (state.background_mode_active) {
                                effects.push_back(HideWindow{});
                            } else {
                                effects.push_back(QuitApplication{});
                            }
                        }
                    },
                    [&state](ui::ContextMenuToggled) {
                        // Restore file search results when toggling back from
                        // context menu
                        if (std::holds_alternative<ui::FileSearch>(
                                state.mode) &&
                            state.cached_file_search_update.has_value()) {

                            const auto &cached =
                                *state.cached_file_search_update;

                            // Restore items (re-process from cached
                            // FileResults)
                            state.items = ui::convert_file_results_to_items(
                                cached.results);
                            state.selected_item_index = 0;
                            state.visible_range_offset = 0;
                        }
                    },
                    [](ui::CursorPositionChanged) {},
                    [&state, &ranker, &global_actions, &desktop_apps,
                     max_visible_items](ui::InputChanged) {
                        state.selected_item_index =
                            0; // Reset selection when search changes
                        state.visible_range_offset = 0; // Reset scroll position

                        // Command palette mode - search utility commands
                        if (!state.input_buffer.empty() &&
                            state.input_buffer[0] == '>') {
                            ranker.pause();

                            const auto query = state.input_buffer.substr(1);
                            state.mode = ui::CommandSearch{.query = query};

                            // For command search, rank all (usually small
                            // dataset)
                            const auto query_lower = to_lower(query);
                            auto ranked = rank(
                                global_actions,
                                [&query_lower](const ui::Item &item) {
                                    return fuzzy::fuzzy_score_5_simd(
                                        item.title + item.description,
                                        query_lower);
                                },
                                global_actions.size());

                            state.items.clear();
                            for (const auto &result : ranked) {
                                state.items.push_back(
                                    global_actions[result.index]);
                            }
                            // App search mode - search desktop applications
                            // only
                        } else if (!state.input_buffer.empty() &&
                                   state.input_buffer[0] == '!') {
                            ranker.pause();

                            const auto query = state.input_buffer.substr(1);
                            state.mode = ui::AppSearch{.query = query};

                            // For app search, rank all (usually small dataset)
                            const auto query_lower = to_lower(query);
                            auto ranked = rank(
                                desktop_apps,
                                [&query_lower](const ApplicationInfo &app) {
                                    return fuzzy::fuzzy_score_5_simd(
                                        app.name + app.description,
                                        query_lower);
                                },
                                desktop_apps.size());

                            state.items.clear();
                            for (const auto &result : ranked) {
                                const auto &app = desktop_apps[result.index];
                                state.items.push_back(ui::Item{
                                    .title = app.name,
                                    .description = app.description,
                                    .path = std::nullopt,
                                    .command =
                                        CustomCommand{.path = std::nullopt,
                                                      .shell_cmd =
                                                          app.exec_command},
                                    .hotkey = std::nullopt,
                                });
                            }
                        } else {
                            // File search mode - activate streaming ranker
                            state.mode =
                                ui::FileSearch{.query = state.input_buffer};

                            ranker.update_query(to_lower(state.input_buffer));
                            ranker.update_requested_count(
                                ui::required_item_count(state,
                                                        max_visible_items));
                            ranker.resume();
                        }
                    }},
                event);
        }

        // Process effects
        bool should_quit = false;
        for (const auto &effect : effects) {
            std::visit(
                overloaded{
                    [&should_quit](const QuitApplication &) {
                        should_quit = true;
                    },
                    [&](const HideWindow &) {
                        window.hide();
                        // Reset UI state for next activation
                        state.input_buffer.clear();
                        state.cursor_position = 0;
                        state.selected_item_index = 0;
                        state.visible_range_offset = 0;
                        state.mode = ui::FileSearch{.query = ""};

                        // Reset history navigation state
                        state.navigating_history = false;
                        state.saved_input_buffer.clear();
                        state.history_position =
                            state.file_search_history.size();

                        // Reset ranker to empty query
                        ranker.update_query("");
                        ranker.update_requested_count(
                            ui::required_item_count(state, max_visible_items));
                        LOG_DEBUG("Window hidden");
                    },
                    [&](const ReloadIndexEffect &) {
                        LOG_INFO("Reloading index...");
                        // Wait for any existing indexing to complete
                        if (index_future.valid()) {
                            index_future.wait();
                        }
                        // Clear the index and restart
                        streaming_index.clear();
                        state.items.clear();
                        state.cached_file_search_update.reset();
                        current_file_results.clear();
                        // Launch new indexer
                        index_future = std::async(std::launch::async, [&]() {
                            indexer::scan_filesystem_streaming(
                                config.index_roots, streaming_index,
                                config.ignore_dirs, config.ignore_dir_names,
                                INDEXER_BATCH_SIZE);
                            LOG_INFO("Scan complete - %zu total files",
                                     streaming_index.get_total_files());
                        });
                        // Re-trigger ranker with current query
                        ranker.update_query(to_lower(state.input_buffer));
                        ranker.update_requested_count(
                            ui::required_item_count(state, max_visible_items));
                    }},
                effect);
        }
        if (should_quit) {
            break;
        }

        // Process streaming result updates
        ResultUpdate update;
        if (result_updates.try_read(update)) {
            if (std::holds_alternative<ui::FileSearch>(state.mode)) {
                // Cache the update for quick restoration from ContextMenu
                state.cached_file_search_update = update;

                current_file_results = std::move(update.results);

                // Convert results to UI items (keep all ranked results for
                // scrolling)
                state.items =
                    ui::convert_file_results_to_items(current_file_results);
                redraw = true;
            }
        }

        // Render UI
        if (redraw) {
            // Calculate and apply window resize before getting context
            const auto max_height = static_cast<unsigned int>(
                window.get_screen_height() * config.height_ratio);
            const size_t current_max_visible_items =
                ui::calculate_max_visible_items(max_height, config.font_size);
            const unsigned int new_height = ui::calculate_window_height(
                config.font_size, state.items.size(),
                current_max_visible_items);

            if (new_height != window.get_height()) {
                window.resize(ui::WindowDimension{.height = new_height,
                                                  .width = window.get_width()});
                state.max_visible_items = ui::calculate_max_visible_items(
                    new_height, config.font_size);
            }
            try {
                window.draw(config, state);
                window.commit_surface();
            } catch (const std::exception &e) {
                LOG_ERROR("Failed to render UI: %s", e.what());
                // Continue running - don't crash on render failure
            }
            redraw = false;
        }
    }

    // Cleanup
    if (state.background_mode_active) {
        window.unregister_global_hotkey();
    }
    if (index_future.valid()) {
        index_future.wait();
    }
    return 0;
}