#include "actions.h"
#include "config.h"
#include "utility.h"

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

void run_command(const std::vector<std::string> &args)
{
    const pid_t pid = fork();
    if (pid == 0) {
        // Detach from parent completely
        setsid();

        // Double fork to avoid zombies
        const pid_t pid2 = fork();
        if (pid2 > 0) {
            _exit(0); // First child exits
        } else if (pid2 == 0) {
            // Grandchild runs the command
            std::vector<char *> argv;
            for (const auto &arg : args) {
                argv.push_back(const_cast<char *>(arg.c_str()));
            }
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            _exit(1);
        }
        _exit(1); // Grandchild is orphaned at this point and will be reaped by
                  // init
    } else if (pid > 0) {
        // Reap first child immediately (it exits right away)
        waitpid(pid, nullptr, 0);
    }
}

void run_custom_command(const CustomCommand &cmd)
{
    const pid_t pid = fork();
    if (pid == 0) {
        setsid();

        const pid_t pid2 = fork();
        if (pid2 > 0) {
            _exit(0);
        } else if (pid2 == 0) {
            if (cmd.path.has_value()) {
                const auto &path = cmd.path.value();
                setenv("FILEPATH", path.string().c_str(), 1);
                setenv("FILENAME", path.filename().string().c_str(), 1);
                setenv("PARENT_DIR", path.parent_path().string().c_str(), 1);
                setenv("EXTENSION", path.extension().string().c_str(), 1);
            }

            execlp("sh", "sh", "-c", cmd.shell_cmd.c_str(), nullptr);
            _exit(1);
        }
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

void copy_to_clipboard(const std::string &content)
{
    int pipefd[2];
    if (pipe(pipefd) == -1)
        return;

    pid_t pid = fork();
    if (pid == 0) {
        // Child: read from pipe, exec xclip
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        execlp("xclip", "xclip", "-selection", "clipboard", nullptr);
        _exit(1);
    } else if (pid > 0) {
        // Parent: write to pipe
        close(pipefd[0]);
        write(pipefd[1], content.data(), content.size());
        close(pipefd[1]);
        waitpid(pid, nullptr, 0);
    }
}

std::string read_file(const fs::path &path)
{
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // anonymous namespace

std::vector<Action> make_file_actions(const fs::path &path,
                                      const Config &config)
{
    std::vector<Action> actions = {
        Action{.title = "Open File",
               .description = config.editor,
               .command = OpenFile{path}},
        Action{.title = "Open Containing Folder",
               .description = config.file_manager,
               .command = OpenContainingFolder{path}},
        Action{.title = "Copy Path to Clipboard",
               .description = "",
               .command = CopyPathToClipboard{path}},
        Action{.title = "Copy Content to Clipboard",
               .description = "",
               .command = CopyContentToClipboard{path}},
    };

    // Append custom file actions, filling in the path
    for (const auto &action_def : config.custom_actions) {
        if (!action_def.is_file_action)
            continue;

        actions.push_back(Action{
            .title = action_def.title,
            .description = action_def.description,
            .command =
                CustomCommand{
                    .path = path,
                    .shell_cmd = action_def.shell_cmd,
                },
        });
    }

    return actions;
}

std::vector<Action> get_global_actions(const Config &config)
{
    std::vector<Action> actions = {
        Action{.title = "Copy ISO Timestamp",
               .description = "Copy current time in ISO 8601 format",
               .command = CopyISOTimestamp{}},
        Action{.title = "Copy Unix Timestamp",
               .description =
                   "Copy current Unix timestamp (seconds since epoch)",
               .command = CopyUnixTimestamp{}},
        Action{.title = "Copy UUID",
               .description = "Generate and copy a new UUID v4",
               .command = CopyUUID{}},
    };

    for (const auto &action_def : config.custom_actions) {
        if (action_def.is_file_action)
            continue;

        actions.push_back(Action{
            .title = action_def.title,
            .description = action_def.description,
            .command =
                CustomCommand{
                    .path = std::nullopt,
                    .shell_cmd = action_def.shell_cmd,
                },
        });
    }
    return actions;
}

void process_command(const Command &cmd, const Config &config)
{
    std::visit(
        overloaded{[&](const OpenFile &cmd) {
                       run_command({config.editor, cmd.path.string()});
                   },
                   [&](const OpenContainingFolder &cmd) {
                       run_command({config.file_manager,
                                    cmd.path.parent_path().string()});
                   },
                   [](const CopyPathToClipboard &cmd) {
                       copy_to_clipboard(cmd.path.string());
                   },
                   [](const CopyContentToClipboard &cmd) {
                       copy_to_clipboard(read_file(cmd.path));
                   },
                   [](const CopyISOTimestamp &) {
                       auto now = std::chrono::system_clock::now();
                       auto time_t = std::chrono::system_clock::to_time_t(now);
                       std::ostringstream oss;
                       oss << std::put_time(std::gmtime(&time_t),
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
                   [](const CustomCommand &cmd) { run_custom_command(cmd); }},
        cmd);
}