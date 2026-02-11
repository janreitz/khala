#pragma once

#include "config.h"
#include "str.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>

namespace ui
{
struct Item;
}

namespace fs = std::filesystem;

// Effects that commands/events can request
// These are processed after event handling completes
struct QuitApplication {
};
struct HideWindow {
};
struct ReloadIndexEffect {
};

using Effect = std::variant<QuitApplication, HideWindow, ReloadIndexEffect>;

typedef enum {
    CMD_NOOP,
    CMD_OPEN_FILE,
    CMD_OPEN_DIRECTORY,
    CMD_REMOVE_FILE,
    CMD_REMOVE_FILE_RECURSIVE,
    CMD_COPY_PATH_TO_CLIPBOARD,
    CMD_COPY_CONTENT_TO_CLIPBOARD,
    CMD_RELOAD_INDEX,
    CMD_COPY_ISO_TIMESTAMP,
    CMD_COPY_UNIX_TIMESTAMP,
    CMD_COPY_UUID,
    CMD_CUSTOM,
} CommandType;

typedef struct {
    Str path; // nullable (data=NULL = no path)
    Str shell_cmd;
    Str shell;
    bool stdout_to_clipboard;
} CustomCommandData;

typedef struct {
    CommandType type;
    union {
        Str path; // CMD_OPEN_FILE .. CMD_COPY_CONTENT_TO_CLIPBOARD
        CustomCommandData custom; // CMD_CUSTOM
    };
} Command;

void cmd_free(Command *cmd);
void cmd_copy(Command *dst, const Command *src);

/// @brief Return false to short-circuit iteration
typedef bool (*ActionCallback)(const void *file_action, void *user_data);
void for_each_file_action(const fs::path &path, const Config &config,
                          ActionCallback cb, void *user_data);

void for_each_global_action(const Config &config, ActionCallback cb,
                            void *user_data);

std::expected<std::optional<Effect>, std::string>
process_command(const Command &cmd, const Config &config);
