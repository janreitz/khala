#include "actions.h"
#include "config.h"
#include "ui.h"
#include "utility.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>

std::vector<ui::Item> make_file_actions(const fs::path &path,
                                        const Config &config)
{
    using ui::KeyboardEvent;
    using ui::KeyCode;
    using ui::KeyModifier;

    if (fs::is_directory(path)) {
        std::vector<ui::Item> items{
            ui::Item{.title = "Open Directory",
                     .description = config.file_manager,
                     .path = std::nullopt,
                     .command = OpenDirectory{path},
                     .hotkey = std::nullopt},
            ui::Item{.title = "Remove Directory",
                     .description = "",
                     .path = std::nullopt,
                     .command = RemoveFile{path},
                     .hotkey = std::nullopt},
            ui::Item{.title = "Remove Directory Recursive",
                     .description = "",
                     .path = std::nullopt,
                     .command = RemoveFileRecursive{path},
                     .hotkey = std::nullopt},
            ui::Item{.title = "Copy Path to Clipboard",
                     .description = "",
                     .path = std::nullopt,
                     .command = CopyPathToClipboard{path},
                     .hotkey = KeyboardEvent{.key = KeyCode::C,
                                             .modifiers = KeyModifier::Ctrl,
                                             .character = std::nullopt}},
        };
        return items;
    } else {
        std::vector<ui::Item> items{
            ui::Item{.title = "Open File",
                     .description = config.editor,
                     .path = std::nullopt,
                     .command = OpenFileCommand{path},
                     .hotkey = std::nullopt},
            ui::Item{.title = "Remove File",
                     .description = "",
                     .path = std::nullopt,
                     .command = RemoveFile{path},
                     .hotkey = std::nullopt},
            ui::Item{.title = "Copy Path to Clipboard",
                     .description = "",
                     .path = std::nullopt,
                     .command = CopyPathToClipboard{path},
                     .hotkey = KeyboardEvent{.key = KeyCode::C,
                                             .modifiers = KeyModifier::Ctrl,
                                             .character = std::nullopt}},
            ui::Item{.title = "Copy Content to Clipboard",
                     .description = "",
                     .path = std::nullopt,
                     .command = CopyContentToClipboard{path},
                     .hotkey = KeyboardEvent{.key = KeyCode::C,
                                             .modifiers = KeyModifier::Ctrl |
                                                          KeyModifier::Shift,
                                             .character = std::nullopt}},
        };
        if (path.has_parent_path()) {
            items.push_back(ui::Item{
                .title = "Open Containing Folder",
                .description = "",
                .path = std::nullopt,
                .command = OpenDirectory{path.parent_path()},
                .hotkey = KeyboardEvent{.key = KeyCode::Return,
                                        .modifiers = KeyModifier::Ctrl,
                                        .character = std::nullopt},
            });
        }

        // Append custom file actions, filling in the path
        for (const auto &action_def : config.custom_actions) {
            if (!action_def.is_file_action)
                continue;

            items.push_back(ui::Item{
                .title = action_def.title,
                .description = action_def.description,
                .path = std::nullopt,
                .command =
                    CustomCommand{
                        .path = path,
                        .shell_cmd = action_def.shell_cmd,
                        .stdout_to_clipboard = action_def.stdout_to_clipboard,
                    },
                .hotkey = std::nullopt,
            });
        }
        return items;
    }
}

std::vector<ui::Item> get_global_actions(const Config &config)
{
    std::vector<ui::Item> items = {
        ui::Item{.title = "Reload Index",
                 .description = "Start a fresh filesystem scan",
                 .path = std::nullopt,
                 .command = ReloadIndex{},
                 .hotkey = std::nullopt},
        ui::Item{.title = "Copy ISO Timestamp",
                 .description = "Copy current time in ISO 8601 format",
                 .path = std::nullopt,
                 .command = CopyISOTimestamp{},
                 .hotkey = std::nullopt},
        ui::Item{.title = "Copy Unix Timestamp",
                 .description =
                     "Copy current Unix timestamp (seconds since epoch)",
                 .path = std::nullopt,
                 .command = CopyUnixTimestamp{},
                 .hotkey = std::nullopt},
        ui::Item{.title = "Copy UUID",
                 .description = "Generate and copy a new UUID v4",
                 .path = std::nullopt,
                 .command = CopyUUID{},
                 .hotkey = std::nullopt},
    };

    for (const auto &action_def : config.custom_actions) {
        if (action_def.is_file_action)
            continue;

        items.push_back(ui::Item{
            .title = action_def.title,
            .description = action_def.description,
            .path = std::nullopt,
            .command =
                CustomCommand{
                    .path = std::nullopt,
                    .shell_cmd = action_def.shell_cmd,
                    .stdout_to_clipboard = action_def.stdout_to_clipboard,
                },
            .hotkey = std::nullopt,
        });
    }
    return items;
}

std::expected<std::optional<Effect>, std::string>
process_command(const Command &cmd, const Config &)
{
    std::optional<Effect> effect;
    try {
        std::visit(
            overloaded{
                [](const Noop &) {},
                [](const OpenFileCommand &open_file) {
                    platform::open_file(open_file.path);
                },
                [](const OpenDirectory &open_dir) {
                    platform::open_directory(open_dir.path);
                },
                [](const RemoveFile &rm_file) { fs::remove(rm_file.path); },
                [](const RemoveFileRecursive &rm_file) {
                    fs::remove_all(rm_file.path);
                },
                [](const CopyPathToClipboard &copy_path) {
                    platform::copy_to_clipboard(copy_path.path.string());
                },
                [](const CopyContentToClipboard &copy_content) {
                    platform::copy_to_clipboard(read_file(copy_content.path));
                },
                [&effect](const ReloadIndex &) {
                    effect = ReloadIndexEffect{};
                },
                [](const CopyISOTimestamp &) {
                    auto now = std::chrono::system_clock::now();
                    auto time_value = std::chrono::system_clock::to_time_t(now);
                    std::ostringstream oss;
                    oss << std::put_time(std::gmtime(&time_value),
                                         "%Y-%m-%dT%H:%M:%SZ");
                    platform::copy_to_clipboard(oss.str());
                },
                [](const CopyUnixTimestamp &) {
                    auto now = std::chrono::system_clock::now();
                    auto seconds =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            now.time_since_epoch())
                            .count();
                    platform::copy_to_clipboard(std::to_string(seconds));
                },
                [](const CopyUUID &) {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> dis(0, 15);
                    std::uniform_int_distribution<> dis2(8, 11);

                    std::ostringstream oss;
                    oss << std::hex;
                    for (int i = 0; i < 8; i++)
                        oss << dis(gen);
                    oss << "-";
                    for (int i = 0; i < 4; i++)
                        oss << dis(gen);
                    oss << "-4";
                    for (int i = 0; i < 3; i++)
                        oss << dis(gen);
                    oss << "-";
                    oss << dis2(gen);
                    for (int i = 0; i < 3; i++)
                        oss << dis(gen);
                    oss << "-";
                    for (int i = 0; i < 12; i++)
                        oss << dis(gen);

                    platform::copy_to_clipboard(oss.str());
                },
                [](const CustomCommand &custom_cmd) {
                    platform::run_custom_command(
                        custom_cmd.shell_cmd, custom_cmd.path,
                        custom_cmd.stdout_to_clipboard);
                }},
            cmd);
        return effect;
    } catch (const std::exception &e) {
        return std::unexpected(e.what());
    }
}