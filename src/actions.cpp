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
    if (fs::is_directory(path)) {
        std::vector<ui::Item> items{
            ui::Item{.title = "Open Directory",
                     .description = config.file_manager,
                     .path = std::nullopt,
                     .command = OpenDirectory{path}},
            ui::Item{.title = "Remove Directory",
                     .description = "",
                     .path = std::nullopt,
                     .command = RemoveFile{path}},
            ui::Item{.title = "Remove Directory Recursive",
                     .description = "",
                     .path = std::nullopt,
                     .command = RemoveFileRecursive{path}},
            ui::Item{.title = "Copy Path to Clipboard",
                     .description = "",
                     .path = std::nullopt,
                     .command = CopyPathToClipboard{path}},
        };
        return items;
    } else {
        std::vector<ui::Item> items{
            ui::Item{.title = "Open File",
                     .description = config.editor,
                     .path = std::nullopt,
                     .command = OpenFileCommand{path}},
            ui::Item{.title = "Remove File",
                     .description = "",
                     .path = std::nullopt,
                     .command = RemoveFile{path}},
            ui::Item{.title = "Copy Path to Clipboard",
                     .description = "",
                     .path = std::nullopt,
                     .command = CopyPathToClipboard{path}},
            ui::Item{.title = "Copy Content to Clipboard",
                     .description = "",
                     .path = std::nullopt,
                     .command = CopyContentToClipboard{path}},
        };
        if (path.has_parent_path()) {
            items.push_back(ui::Item{
                .title = "Open Containing Folder",
                .description = config.file_manager,
                .path = std::nullopt,
                .command = OpenDirectory{path.parent_path()},
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
            });
        }
        return items;
    }
}

std::vector<ui::Item> get_global_actions(const Config &config)
{
    std::vector<ui::Item> items = {
        ui::Item{.title = "Copy ISO Timestamp",
                 .description = "Copy current time in ISO 8601 format",
                 .path = std::nullopt,
                 .command = CopyISOTimestamp{}},
        ui::Item{.title = "Copy Unix Timestamp",
                 .description =
                     "Copy current Unix timestamp (seconds since epoch)",
                .path = std::nullopt,
                 .command = CopyUnixTimestamp{}},
        ui::Item{.title = "Copy UUID",
                 .description = "Generate and copy a new UUID v4",
                 .path = std::nullopt,
                 .command = CopyUUID{}},
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
        });
    }
    return items;
}

std::optional<std::string> process_command(const Command &cmd,
                                           const Config &config)
{
    try {
        std::visit(
            overloaded{
                [&](const OpenFileCommand &open_file) {
                    run_command({config.editor, open_file.path.string()});
                },
                [&](const OpenDirectory &open_dir) {
                    run_command(
                        {config.file_manager, open_dir.path.parent_path().string()});
                },
                [](const RemoveFile &rm_file) {
                    fs::remove(rm_file.path);
                },
                [](const RemoveFileRecursive &rm_file) {
                    fs::remove_all(rm_file.path);
                },
                [](const CopyPathToClipboard &copy_path) {
                    copy_to_clipboard(copy_path.path.string());
                },
                [](const CopyContentToClipboard &copy_content) {
                    copy_to_clipboard(read_file(copy_content.path));
                },
                [](const CopyISOTimestamp &) {
                    auto now = std::chrono::system_clock::now();
                    auto time_value = std::chrono::system_clock::to_time_t(now);
                    std::ostringstream oss;
                    oss << std::put_time(std::gmtime(&time_value),
                                         "%Y-%m-%dT%H:%M:%SZ");
                    copy_to_clipboard(oss.str());
                },
                [](const CopyUnixTimestamp &) {
                    auto now = std::chrono::system_clock::now();
                    auto seconds =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            now.time_since_epoch())
                            .count();
                    copy_to_clipboard(std::to_string(seconds));
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

                    copy_to_clipboard(oss.str());
                },
                [](const CustomCommand &custom_cmd) { run_custom_command(custom_cmd.shell_cmd, custom_cmd.path, custom_cmd.stdout_to_clipboard); }},
            cmd);
        return std::nullopt;
    } catch (const std::exception &e) {
        return std::string(e.what());
    }
}