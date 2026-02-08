#include "actions.h"
#include "config.h"
#include "types.h"
#include "ui.h"
#include "utility.h"

#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <ios>
#include <optional>
#include <random>
#include <sstream>
#include <string>

#include <cerrno>
#include <cstring>
#include <iomanip>
#include <variant>

void for_each_file_action(const fs::path &path, const Config &config,
                          ActionCallback cb, void *user_data)
{
    if (fs::is_directory(path)) {
        const ui::Item open_dir{.title = str_from_literal("Open Directory"),
                                .description = str_from_std_string(config.file_manager),
                                .path = std::nullopt,
                                .command = OpenDirectory{path},
                                .hotkey = std::nullopt};
        if (!cb(&open_dir, user_data)) return;

        const ui::Item rm_dir{.title = str_from_literal("Remove Directory"),
                              .description = str_from_literal(""),
                              .path = std::nullopt,
                              .command = RemoveFile{path},
                              .hotkey = std::nullopt};
        if (!cb(&rm_dir, user_data)) return;

        const ui::Item rm_dir_recursive{.title = str_from_literal("Remove Directory Recursive"),
                                        .description = str_from_literal(""),
                                        .path = std::nullopt,
                                        .command = RemoveFileRecursive{path},
                                        .hotkey = std::nullopt};
        if (!cb(&rm_dir_recursive, user_data)) return;

        const ui::Item cp_to_clipboard{
            .title = str_from_literal("Copy Path to Clipboard"),
            .description = str_from_literal(""),
            .path = std::nullopt,
            .command = CopyPathToClipboard{path},
            .hotkey = ui::KeyboardEvent{.key = ui::KeyCode::C,
                                        .modifiers = ui::KeyModifier::Ctrl,
                                        .character = std::nullopt}};
        if (!cb(&cp_to_clipboard, user_data)) return;

        // Append custom directory actions
        for (size_t i = 0; i < config.custom_action_defs.count; i++) {
            const auto *action_def = static_cast<const CustomActionDef *>(
                vec_at(&config.custom_action_defs, i));
            if (action_def->action_type != ActionType::Directory)
                continue;

            const ui::Item custom_action{
                .title = str_from_std_string(action_def->title),
                .description = str_from_std_string(action_def->description),
                .path = std::nullopt,
                .command =
                    CustomCommand{
                        .path = path,
                        .shell_cmd = action_def->shell_cmd,
                        .shell =
                            action_def->shell.value_or(config.default_shell),
                        .stdout_to_clipboard = action_def->stdout_to_clipboard,
                    },
                .hotkey = action_def->hotkey,
            };
            if (!cb(&custom_action, user_data)) return;
        }
        return;
    } else {
        const ui::Item open_file = {.title = str_from_literal("Open File"),
                                    .description = str_from_std_string(config.editor),
                                    .path = std::nullopt,
                                    .command = OpenFileCommand{path},
                                    .hotkey = std::nullopt};
        if (!cb(&open_file, user_data)) return;
        const ui::Item rm_file = {.title = str_from_literal("Remove File"),
                                  .description = str_from_literal(""),
                                  .path = std::nullopt,
                                  .command = RemoveFile{path},
                                  .hotkey = std::nullopt};
        if (!cb(&rm_file, user_data)) return;
        const ui::Item cp_path = {
            .title = str_from_literal("Copy Path to Clipboard"),
            .description = str_from_literal(""),
            .path = std::nullopt,
            .command = CopyPathToClipboard{path},
            .hotkey = ui::KeyboardEvent{.key = ui::KeyCode::C,
                                        .modifiers = ui::KeyModifier::Ctrl,
                                        .character = std::nullopt}};
        if (!cb(&cp_path, user_data)) return;
        const ui::Item cp_content = {
            .title = str_from_literal("Copy Content to Clipboard"),
            .description = str_from_literal(""),
            .path = std::nullopt,
            .command = CopyContentToClipboard{path},
            .hotkey = ui::KeyboardEvent{.key = ui::KeyCode::C,
                                        .modifiers = ui::KeyModifier::Ctrl |
                                                     ui::KeyModifier::Shift,
                                        .character = std::nullopt}};
        if (!cb(&cp_content, user_data)) return;

        if (path.has_parent_path()) {
            const ui::Item open_folder = {
                .title = str_from_literal("Open Containing Folder"),
                .description = str_from_literal(""),
                .path = std::nullopt,
                .command = OpenDirectory{path.parent_path()},
                .hotkey = ui::KeyboardEvent{.key = ui::KeyCode::Return,
                                            .modifiers = ui::KeyModifier::Ctrl,
                                            .character = std::nullopt},
            };
            if (!cb(&open_folder, user_data)) return;
        }

        // Append custom file actions, filling in the path
        for (size_t i = 0; i < config.custom_action_defs.count; i++) {
            const auto *action_def = static_cast<const CustomActionDef *>(
                vec_at(&config.custom_action_defs, i));
            if (action_def->action_type != ActionType::File)
                continue;

            const ui::Item custom_action = {
                .title = str_from_std_string(action_def->title),
                .description = str_from_std_string(action_def->description),
                .path = std::nullopt,
                .command =
                    CustomCommand{
                        .path = path,
                        .shell_cmd = action_def->shell_cmd,
                        .shell =
                            action_def->shell.value_or(config.default_shell),
                        .stdout_to_clipboard = action_def->stdout_to_clipboard,
                    },
                .hotkey = action_def->hotkey,
            };
            if (!cb(&custom_action, user_data)) return;
        }
    }
}

void for_each_global_action(const Config &config, ActionCallback cb,
                            void *user_data)
{
    const ui::Item reload_index = {.title = str_from_literal("Reload Index"),
                                   .description =
                                       str_from_literal("Start a fresh filesystem scan"),
                                   .path = std::nullopt,
                                   .command = ReloadIndex{},
                                   .hotkey = std::nullopt};
    if (!cb(&reload_index, user_data)) return;
    const ui::Item cp_iso_timestamp = {
        .title = str_from_literal("Copy ISO Timestamp"),
        .description = str_from_literal("Copy current time in ISO 8601 format"),
        .path = std::nullopt,
        .command = CopyISOTimestamp{},
        .hotkey = std::nullopt};
    if (!cb(&cp_iso_timestamp, user_data)) return;
    const ui::Item cp_unix_timestamp = {
        .title = str_from_literal("Copy Unix Timestamp"),
        .description = str_from_literal("Copy current Unix timestamp (seconds since epoch)"),
        .path = std::nullopt,
        .command = CopyUnixTimestamp{},
        .hotkey = std::nullopt};
    if (!cb(&cp_unix_timestamp, user_data)) return;
    const ui::Item cp_uuid = {.title = str_from_literal("Copy UUID"),
                              .description = str_from_literal("Generate and copy a new UUID v4"),
                              .path = std::nullopt,
                              .command = CopyUUID{},
                              .hotkey = std::nullopt};
    if (!cb(&cp_uuid, user_data)) return;

    for (size_t i = 0; i < config.custom_action_defs.count; i++) {
        const auto *action_def = static_cast<const CustomActionDef *>(
            vec_at(&config.custom_action_defs, i));
        if (action_def->action_type != ActionType::Utility)
            continue;

        const ui::Item custom_action = {
            .title = str_from_std_string(action_def->title),
            .description = str_from_std_string(action_def->description),
            .path = std::nullopt,
            .command =
                CustomCommand{
                    .path = std::nullopt,
                    .shell_cmd = action_def->shell_cmd,
                    .shell = action_def->shell.value_or(config.default_shell),
                    .stdout_to_clipboard = action_def->stdout_to_clipboard,
                },
            .hotkey = action_def->hotkey,
        };
        if (!cb(&custom_action, user_data)) return;
    }
}

#undef EMIT

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
                        custom_cmd.stdout_to_clipboard, custom_cmd.shell);
                }},
            cmd);
        return effect;
    } catch (const std::exception &e) {
        return std::unexpected(e.what());
    }
}