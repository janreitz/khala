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

void cmd_free(Command *cmd)
{
    switch (cmd->type) {
    case CMD_OPEN_FILE:
    case CMD_OPEN_DIRECTORY:
    case CMD_REMOVE_FILE:
    case CMD_REMOVE_FILE_RECURSIVE:
    case CMD_COPY_PATH_TO_CLIPBOARD:
    case CMD_COPY_CONTENT_TO_CLIPBOARD:
        str_free(&cmd->path);
        break;
    case CMD_CUSTOM:
        str_free(&cmd->custom.path);
        str_free(&cmd->custom.shell_cmd);
        str_free(&cmd->custom.shell);
        break;
    case CMD_NOOP:
    case CMD_RELOAD_INDEX:
    case CMD_COPY_ISO_TIMESTAMP:
    case CMD_COPY_UNIX_TIMESTAMP:
    case CMD_COPY_UUID:
        break;
    }
    cmd->type = CMD_NOOP;
}

void cmd_copy(Command *dst, const Command *src)
{
    cmd_free(dst);

    switch (src->type) {
    case CMD_OPEN_FILE:
    case CMD_OPEN_DIRECTORY:
    case CMD_REMOVE_FILE:
    case CMD_REMOVE_FILE_RECURSIVE:
    case CMD_COPY_PATH_TO_CLIPBOARD:
    case CMD_COPY_CONTENT_TO_CLIPBOARD: {
        Str temp_path = {nullptr, 0, 0};
        if (!str_copy(&temp_path, &src->path))
            return;
        dst->type = src->type;
        dst->path = temp_path;
        break;
    }
    case CMD_CUSTOM: {
        Str temp_path = {nullptr, 0, 0};
        Str temp_shell_cmd = {nullptr, 0, 0};
        Str temp_shell = {nullptr, 0, 0};

        if (!str_copy(&temp_path, &src->custom.path))
            return;
        if (!str_copy(&temp_shell_cmd, &src->custom.shell_cmd)) {
            str_free(&temp_path);
            return;
        }
        if (!str_copy(&temp_shell, &src->custom.shell)) {
            str_free(&temp_path);
            str_free(&temp_shell_cmd);
            return;
        }

        dst->type = CMD_CUSTOM;
        dst->custom.path = temp_path;
        dst->custom.shell_cmd = temp_shell_cmd;
        dst->custom.shell = temp_shell;
        dst->custom.stdout_to_clipboard = src->custom.stdout_to_clipboard;
        break;
    }
    case CMD_NOOP:
    case CMD_RELOAD_INDEX:
    case CMD_COPY_ISO_TIMESTAMP:
    case CMD_COPY_UNIX_TIMESTAMP:
    case CMD_COPY_UUID:
        dst->type = src->type;
        break;
    }
}

void for_each_file_action(const fs::path &path, const Config &config,
                          ActionCallback cb, void *user_data)
{
    if (fs::is_directory(path)) {
        ui::Item open_dir{
            .title = str_from_literal("Open Directory"),
            .description = str_from_std_string(config.file_manager),
            .path = {nullptr, 0, 0},
            .command = {.type = CMD_OPEN_DIRECTORY,
                        .path = str_from_std_string(path.string())},
            .hotkey = std::nullopt};
        bool cont = cb(&open_dir, user_data);
        ui::ui_item_free(&open_dir, nullptr);
        if (!cont)
            return;

        ui::Item rm_dir{
            .title = str_from_literal("Remove Directory"),
            .description = str_from_literal(""),
            .path = {nullptr, 0, 0},
            .command = {.type = CMD_REMOVE_FILE,
                        .path = str_from_std_string(path.string())},
            .hotkey = std::nullopt};
        cont = cb(&rm_dir, user_data);
        ui::ui_item_free(&rm_dir, nullptr);
        if (!cont)
            return;

        ui::Item rm_dir_recursive{
            .title = str_from_literal("Remove Directory Recursive"),
            .description = str_from_literal(""),
            .path = {nullptr, 0, 0},
            .command = {.type = CMD_REMOVE_FILE_RECURSIVE,
                        .path = str_from_std_string(path.string())},
            .hotkey = std::nullopt};
        cont = cb(&rm_dir_recursive, user_data);
        ui::ui_item_free(&rm_dir_recursive, nullptr);
        if (!cont)
            return;

        ui::Item cp_to_clipboard{
            .title = str_from_literal("Copy Path to Clipboard"),
            .description = str_from_literal(""),
            .path = {nullptr, 0, 0},
            .command = {.type = CMD_COPY_PATH_TO_CLIPBOARD,
                        .path = str_from_std_string(path.string())},
            .hotkey = ui::KeyboardEvent{.key = ui::KeyCode::C,
                                        .modifiers = ui::KeyModifier::Ctrl,
                                        .character = std::nullopt}};
        cont = cb(&cp_to_clipboard, user_data);
        ui::ui_item_free(&cp_to_clipboard, nullptr);
        if (!cont)
            return;

        // Append custom directory actions
        for (size_t i = 0; i < config.custom_action_defs.count; i++) {
            const auto *action_def = static_cast<const CustomActionDef *>(
                vec_at(&config.custom_action_defs, i));
            if (action_def->action_type != ActionType::Directory)
                continue;

            ui::Item custom_action{
                .title = action_def->title,
                .description = action_def->description,
                .path = {nullptr, 0, 0},
                .command = {.type = CMD_CUSTOM,
                            .custom =
                                {
                                    .path = str_from_std_string(path.string()),
                                    .shell_cmd = action_def->shell_cmd,
                                    .shell = action_def->shell,
                                    .stdout_to_clipboard =
                                        action_def->stdout_to_clipboard,
                                }},
                .hotkey = action_def->hotkey,
            };
            cont = cb(&custom_action, user_data);
            // Only free the owned path; title/description/shell fields are
            // borrowed from config
            str_free(&custom_action.command.custom.path);
            if (!cont)
                return;
        }
        return;
    } else {
        ui::Item open_file = {
            .title = str_from_literal("Open File"),
            .description = str_from_std_string(config.editor),
            .path = {nullptr, 0, 0},
            .command = {.type = CMD_OPEN_FILE,
                        .path = str_from_std_string(path.string())},
            .hotkey = std::nullopt};
        bool cont = cb(&open_file, user_data);
        ui::ui_item_free(&open_file, nullptr);
        if (!cont)
            return;

        ui::Item rm_file = {
            .title = str_from_literal("Remove File"),
            .description = str_from_literal(""),
            .path = {nullptr, 0, 0},
            .command = {.type = CMD_REMOVE_FILE,
                        .path = str_from_std_string(path.string())},
            .hotkey = std::nullopt};
        cont = cb(&rm_file, user_data);
        ui::ui_item_free(&rm_file, nullptr);
        if (!cont)
            return;

        ui::Item cp_path = {
            .title = str_from_literal("Copy Path to Clipboard"),
            .description = str_from_literal(""),
            .path = {nullptr, 0, 0},
            .command = {.type = CMD_COPY_PATH_TO_CLIPBOARD,
                        .path = str_from_std_string(path.string())},
            .hotkey = ui::KeyboardEvent{.key = ui::KeyCode::C,
                                        .modifiers = ui::KeyModifier::Ctrl,
                                        .character = std::nullopt}};
        cont = cb(&cp_path, user_data);
        ui::ui_item_free(&cp_path, nullptr);
        if (!cont)
            return;

        ui::Item cp_content = {
            .title = str_from_literal("Copy Content to Clipboard"),
            .description = str_from_literal(""),
            .path = {nullptr, 0, 0},
            .command = {.type = CMD_COPY_CONTENT_TO_CLIPBOARD,
                        .path = str_from_std_string(path.string())},
            .hotkey = ui::KeyboardEvent{.key = ui::KeyCode::C,
                                        .modifiers = ui::KeyModifier::Ctrl |
                                                     ui::KeyModifier::Shift,
                                        .character = std::nullopt}};
        cont = cb(&cp_content, user_data);
        ui::ui_item_free(&cp_content, nullptr);
        if (!cont)
            return;

        if (path.has_parent_path()) {
            ui::Item open_folder = {
                .title = str_from_literal("Open Containing Folder"),
                .description = str_from_literal(""),
                .path = {nullptr, 0, 0},
                .command = {.type = CMD_OPEN_DIRECTORY,
                            .path = str_from_std_string(
                                path.parent_path().string())},
                .hotkey = ui::KeyboardEvent{.key = ui::KeyCode::Return,
                                            .modifiers = ui::KeyModifier::Ctrl,
                                            .character = std::nullopt},
            };
            cont = cb(&open_folder, user_data);
            ui::ui_item_free(&open_folder, nullptr);
            if (!cont)
                return;
        }

        // Append custom file actions, filling in the path
        for (size_t i = 0; i < config.custom_action_defs.count; i++) {
            const auto *action_def = static_cast<const CustomActionDef *>(
                vec_at(&config.custom_action_defs, i));
            if (action_def->action_type != ActionType::File)
                continue;

            ui::Item custom_action = {
                .title = action_def->title,
                .description = action_def->description,
                .path = {nullptr, 0, 0},
                .command = {.type = CMD_CUSTOM,
                            .custom =
                                {
                                    .path = str_from_std_string(path.string()),
                                    .shell_cmd =
                                        action_def->shell_cmd,
                                    .shell =
                                        action_def->shell,
                                    .stdout_to_clipboard =
                                        action_def->stdout_to_clipboard,
                                }},
                .hotkey = action_def->hotkey,
            };
            cont = cb(&custom_action, user_data);
            // Only free the owned path; title/description/shell fields are
            // borrowed from config
            str_free(&custom_action.command.custom.path);
            if (!cont)
                return;
        }
    }
}

void for_each_global_action(const Config &config, ActionCallback cb,
                            void *user_data)
{
    ui::Item reload_index = {
        .title = str_from_literal("Reload Index"),
        .description = str_from_literal("Start a fresh filesystem scan"),
        .path = {nullptr, 0, 0},
        .command = {.type = CMD_RELOAD_INDEX, .path = {nullptr, 0, 0}},
        .hotkey = std::nullopt};
    bool cont = cb(&reload_index, user_data);
    ui::ui_item_free(&reload_index, nullptr);
    if (!cont)
        return;

    ui::Item cp_iso_timestamp = {
        .title = str_from_literal("Copy ISO Timestamp"),
        .description = str_from_literal("Copy current time in ISO 8601 format"),
        .path = {nullptr, 0, 0},
        .command = {.type = CMD_COPY_ISO_TIMESTAMP, .path = {nullptr, 0, 0}},
        .hotkey = std::nullopt};
    cont = cb(&cp_iso_timestamp, user_data);
    ui::ui_item_free(&cp_iso_timestamp, nullptr);
    if (!cont)
        return;

    ui::Item cp_unix_timestamp = {
        .title = str_from_literal("Copy Unix Timestamp"),
        .description = str_from_literal(
            "Copy current Unix timestamp (seconds since epoch)"),
        .path = {nullptr, 0, 0},
        .command = {.type = CMD_COPY_UNIX_TIMESTAMP, .path = {nullptr, 0, 0}},
        .hotkey = std::nullopt};
    cont = cb(&cp_unix_timestamp, user_data);
    ui::ui_item_free(&cp_unix_timestamp, nullptr);
    if (!cont)
        return;

    ui::Item cp_uuid = {
        .title = str_from_literal("Copy UUID"),
        .description = str_from_literal("Generate and copy a new UUID v4"),
        .path = {nullptr, 0, 0},
        .command = {.type = CMD_COPY_UUID, .path = {nullptr, 0, 0}},
        .hotkey = std::nullopt};
    cont = cb(&cp_uuid, user_data);
    ui::ui_item_free(&cp_uuid, nullptr);
    if (!cont)
        return;

    // Custom utility actions — all fields are borrowed from config, nothing to
    // free
    for (size_t i = 0; i < config.custom_action_defs.count; i++) {
        const auto *action_def = static_cast<const CustomActionDef *>(
            vec_at(&config.custom_action_defs, i));
        if (action_def->action_type != ActionType::Utility)
            continue;

        const ui::Item custom_action = {
            .title = action_def->title,
            .description = action_def->description,
            .path = {nullptr, 0, 0},
            .command =
                {.type = CMD_CUSTOM,
                 .custom =
                     {
                         .path = {nullptr, 0, 0},
                         .shell_cmd =
                             action_def->shell_cmd,
                         .shell =
                             action_def->shell,
                         .stdout_to_clipboard = action_def->stdout_to_clipboard,
                     }},
            .hotkey = action_def->hotkey,
        };
        if (!cb(&custom_action, user_data))
            return;
    }
}

std::expected<std::optional<Effect>, std::string>
process_command(const Command &cmd, const Config &)
{
    std::optional<Effect> effect;
    try {
        switch (cmd.type) {
        case CMD_NOOP:
            break;
        case CMD_OPEN_FILE:
            platform::open_file(fs::path(cmd.path.data));
            break;
        case CMD_OPEN_DIRECTORY:
            platform::open_directory(fs::path(cmd.path.data));
            break;
        case CMD_REMOVE_FILE:
            fs::remove(fs::path(cmd.path.data));
            break;
        case CMD_REMOVE_FILE_RECURSIVE:
            fs::remove_all(fs::path(cmd.path.data));
            break;
        case CMD_COPY_PATH_TO_CLIPBOARD:
            platform::copy_to_clipboard(fs::path(cmd.path.data).string());
            break;
        case CMD_COPY_CONTENT_TO_CLIPBOARD:
            platform::copy_to_clipboard(read_file(fs::path(cmd.path.data)));
            break;
        case CMD_RELOAD_INDEX:
            effect = ReloadIndexEffect{};
            break;
        case CMD_COPY_ISO_TIMESTAMP: {
            auto now = std::chrono::system_clock::now();
            auto time_value = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::gmtime(&time_value),
                                 "%Y-%m-%dT%H:%M:%SZ");
            platform::copy_to_clipboard(oss.str());
            break;
        }
        case CMD_COPY_UNIX_TIMESTAMP: {
            auto now = std::chrono::system_clock::now();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                               now.time_since_epoch())
                               .count();
            platform::copy_to_clipboard(std::to_string(seconds));
            break;
        }
        case CMD_COPY_UUID: {
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
            break;
        }
        case CMD_CUSTOM: {
            std::optional<fs::path> opt_path;
            if (cmd.custom.path.data != nullptr) {
                opt_path = fs::path(cmd.custom.path.data);
            }
            platform::run_custom_command(
                std::string(cmd.custom.shell_cmd.data,
                            cmd.custom.shell_cmd.len),
                opt_path, cmd.custom.stdout_to_clipboard,
                std::string(cmd.custom.shell.data, cmd.custom.shell.len));
            break;
        }
        }
        return effect;
    } catch (const std::exception &e) {
        return std::unexpected(e.what());
    }
}
