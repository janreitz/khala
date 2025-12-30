#include "actions.h"
#include "utility.h"

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <random>



namespace {

void run_command(const std::vector<std::string>& args) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        std::vector<char*> argv;
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(argv[0], argv.data());
        _exit(1); // exec failed
    } else if (pid > 0) {
        // Parent - wait for child
        waitpid(pid, nullptr, 0);
    }
}

void copy_to_clipboard(const std::string& content) {
    int pipefd[2];
    if (pipe(pipefd) == -1) return;
    
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

std::string read_file(const fs::path& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // anonymous namespace

std::vector<Action> make_file_actions(const fs::path &path)
{
    return {
        Action{.title = "Open File",
               .description = "Description...",
               .command = OpenFile{path}},
        Action{.title = "Open Containing Folder",
               .description = "",
               .command = OpenContainingFolder{path}},
        Action{.title = "Copy Path to Clipboard",
               .description = "",
               .command = CopyPathToClipboard{path}},
        Action{.title = "Copy Content to Clipboard",
               .description = "",
               .command = CopyContentToClipboard{path}},
    };
}

const std::vector<Action>& get_utility_actions()
{
    static const std::vector<Action> actions = {
        Action{.title = "Copy ISO Timestamp",
               .description = "Copy current time in ISO 8601 format",
               .command = CopyISOTimestamp{}},
        Action{.title = "Copy Unix Timestamp",
               .description = "Copy current Unix timestamp (seconds since epoch)",
               .command = CopyUnixTimestamp{}},
        Action{.title = "Copy UUID",
               .description = "Generate and copy a new UUID v4",
               .command = CopyUUID{}},
    };
    return actions;
}

void process_command(const Command& cmd) {
    std::visit(overloaded{
        [](const OpenFile& c) {
            run_command({"xdg-open", c.path.string()});
        },
        [](const OpenContainingFolder& c) {
            run_command({"xdg-open", c.path.parent_path().string()});
        },
        [](const CopyPathToClipboard& c) {
            copy_to_clipboard(c.path.string());
        },
        [](const CopyContentToClipboard& c) {
            copy_to_clipboard(read_file(c.path));
        },
        [](const CopyISOTimestamp&) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
            copy_to_clipboard(oss.str());
        },
        [](const CopyUnixTimestamp&) {
            auto now = std::chrono::system_clock::now();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            copy_to_clipboard(std::to_string(seconds));
        },
        [](const CopyUUID&) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 15);
            std::uniform_int_distribution<> dis2(8, 11);

            std::ostringstream oss;
            oss << std::hex;
            for (int i = 0; i < 8; i++) oss << dis(gen);
            oss << "-";
            for (int i = 0; i < 4; i++) oss << dis(gen);
            oss << "-4";
            for (int i = 0; i < 3; i++) oss << dis(gen);
            oss << "-";
            oss << dis2(gen);
            for (int i = 0; i < 3; i++) oss << dis(gen);
            oss << "-";
            for (int i = 0; i < 12; i++) oss << dis(gen);

            copy_to_clipboard(oss.str());
        },
    }, cmd);
}