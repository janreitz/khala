#include "utility.h"

#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

namespace platform
{

namespace fs = std::filesystem;

std::string path_to_string(const fs::path &path) { return path.string(); }

std::optional<std::filesystem::path> get_home_dir()
{
    const char *home = std::getenv("HOME");

    if (!home) {
        return std::nullopt;
    }
    const fs::path home_path(home);
    if (!fs::exists(home_path)) {
        return std::nullopt;
    }
    return home_path;
}

fs::path get_temp_dir()
{
    const char *temp = std::getenv("TMPDIR");
    if (!temp) {
        temp = "/tmp";
    }
    return fs::path(temp);
}

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

void run_custom_command_with_capture(const std::string &cmd,
                                     const std::optional<fs::path> &path)
{
    if (cmd.empty()) {
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

        if (path.has_value()) {
            const auto &_path = path.value();
            setenv("FILEPATH", _path.string().c_str(), 1);
            setenv("FILENAME", _path.filename().string().c_str(), 1);
            setenv("PARENT_DIR", _path.parent_path().string().c_str(), 1);
            setenv("EXTENSION", _path.extension().string().c_str(), 1);
        }

        execlp("sh", "sh", "-c", cmd.c_str(), nullptr);
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
            throw std::runtime_error("Custom command failed: " + cmd);
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

void run_custom_command(const std::string &cmd,
                        const std::optional<fs::path> &path,
                        bool stdout_to_clipboard)
{
    if (cmd.empty()) {
        throw std::runtime_error("Custom command is empty");
    }

    if (stdout_to_clipboard) {
        run_custom_command_with_capture(cmd, path);
    }

    const pid_t pid = fork();
    if (pid == 0) {
        setsid();

        const pid_t pid2 = fork();
        if (pid2 > 0) {
            _exit(0);
        } else if (pid2 == 0) {
            if (path.has_value()) {
                const auto &_path = path.value();
                setenv("FILEPATH", _path.string().c_str(), 1);
                setenv("FILENAME", _path.filename().string().c_str(), 1);
                setenv("PARENT_DIR", _path.parent_path().string().c_str(), 1);
                setenv("EXTENSION", _path.extension().string().c_str(), 1);
            }

            execlp("sh", "sh", "-c", cmd.c_str(), nullptr);
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
            throw std::runtime_error("Custom command failed: " + cmd);
        }
    } else {
        throw std::runtime_error("Failed to fork process for custom command: " +
                                 std::string(strerror(errno)));
    }
}

void open_file(const fs::path& path) {
    run_command({"xdg-open", path.string()});
}

void open_directory(const fs::path &path) {
    run_command({"xdg-open", path.string()});
}

} // namespace platform