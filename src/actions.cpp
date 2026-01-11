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
#include <sys/wait.h>
#include <unistd.h>

namespace
{

void copy_to_clipboard(const std::string &content)
{
    int pipefd[2];
    if (pipe(pipefd) == -1)
        throw std::runtime_error("Failed to create pipe for clipboard: " +
                                 std::string(strerror(errno)));

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
        ssize_t bytes_written =
            write(pipefd[1], content.data(), content.size());
        int write_errno = errno; // Save errno before close() might change it
        close(pipefd[1]);

        if (bytes_written == -1) {
            throw std::runtime_error("Failed to write to clipboard pipe: " +
                                     std::string(strerror(write_errno)));
        }
        if (static_cast<size_t>(bytes_written) != content.size()) {
            throw std::runtime_error("Incomplete write to clipboard pipe");
        }
        int status;
        waitpid(pid, &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            throw std::runtime_error(
                "xclip command failed - clipboard may not be available");
        }
    } else {
        throw std::runtime_error("Failed to fork process for clipboard: " +
                                 std::string(strerror(errno)));
    }
}

void run_command(const std::vector<std::string> &args)
{
    if (args.empty()) {
        throw std::runtime_error("No command specified");
    }

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
        } else {
            throw std::runtime_error("Failed to fork grandchild process: " +
                                     std::string(strerror(errno)));
        }
        _exit(1);
    } else if (pid > 0) {
        // Reap first child immediately (it exits right away)
        int status;
        waitpid(pid, &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            throw std::runtime_error("Failed to launch command: " + args[0]);
        }
    } else {
        throw std::runtime_error("Failed to fork process: " +
                                 std::string(strerror(errno)));
    }
}

void run_custom_command_with_capture(const CustomCommand &cmd)
{
    if (cmd.shell_cmd.empty()) {
        throw std::runtime_error("Custom command is empty");
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        throw std::runtime_error(
            "Failed to create pipe for custom command output: " +
            std::string(strerror(errno)));
    }

    const pid_t pid = fork();
    if (pid == 0) {
        // Child process
        close(pipefd[0]);               // Close read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]);

        if (cmd.path.has_value()) {
            const auto &path = cmd.path.value();
            setenv("FILEPATH", path.string().c_str(), 1);
            setenv("FILENAME", path.filename().string().c_str(), 1);
            setenv("PARENT_DIR", path.parent_path().string().c_str(), 1);
            setenv("EXTENSION", path.extension().string().c_str(), 1);
        }

        execlp("sh", "sh", "-c", cmd.shell_cmd.c_str(), nullptr);
        _exit(1);
    } else if (pid > 0) {
        // Parent process
        close(pipefd[1]); // Close write end

        // Read the output
        std::string output;
        char buffer[4096];
        ssize_t bytes_read;
        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            output.append(buffer, static_cast<size_t>(bytes_read));
        }
        close(pipefd[0]);

        // Wait for child to finish
        int status;
        waitpid(pid, &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            throw std::runtime_error("Custom command failed: " + cmd.shell_cmd);
        }

        // Remove trailing newline if present
        if (!output.empty() && output.back() == '\n') {
            output.pop_back();
        }

        // Copy to clipboard
        if (!output.empty()) {
            copy_to_clipboard(output);
        }
    } else {
        throw std::runtime_error("Failed to fork process for custom command: " +
                                 std::string(strerror(errno)));
    }
}

void run_custom_command(const CustomCommand &cmd)
{
    if (cmd.shell_cmd.empty()) {
        throw std::runtime_error("Custom command is empty");
    }

    if (cmd.stdout_to_clipboard) {
        run_custom_command_with_capture(cmd);
        return;
    }

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
        } else {
            throw std::runtime_error(
                "Failed to fork grandchild process for custom command: " +
                std::string(strerror(errno)));
        }
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            throw std::runtime_error("Custom command failed: " + cmd.shell_cmd);
        }
    } else {
        throw std::runtime_error("Failed to fork process for custom command: " +
                                 std::string(strerror(errno)));
    }
}

std::string read_file(const fs::path &path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    if (file.bad()) {
        throw std::runtime_error("Error reading file: " + path.string());
    }

    return buffer.str();
}

} // anonymous namespace

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
                [](const CustomCommand &custom_cmd) { run_custom_command(custom_cmd); }},
            cmd);
        return std::nullopt;
    } catch (const std::exception &e) {
        return std::string(e.what());
    }
}