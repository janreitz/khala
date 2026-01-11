#include "utility.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <fstream>

#ifdef PLATFORM_WIN32
#include <Windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

std::string serialize_file_info(const fs::path &path)
{
    std::ostringstream oss;
    std::error_code ec;

    auto status = fs::status(path, ec);
    if (ec) {
        return "Error: " + ec.message();
    }

    // Human-readable file size
    auto format_size = [](uintmax_t bytes) -> std::string {
        const char *units[] = {"B", "K", "M", "G", "T", "P"};
        int unit_index = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit_index < 5) {
            size /= 1024.0;
            ++unit_index;
        }

        std::ostringstream s;
        if (unit_index == 0) {
            s << bytes << units[unit_index];
        } else {
            s << std::fixed << std::setprecision(1) << size
              << units[unit_index];
        }
        return s.str();
    };

    // File type indicator
    auto type = status.type();
    char type_char = '-';
    if (type == fs::file_type::directory)
        type_char = 'd';
    else if (type == fs::file_type::symlink)
        type_char = 'l';
    else if (type == fs::file_type::block)
        type_char = 'b';
    else if (type == fs::file_type::character)
        type_char = 'c';
    else if (type == fs::file_type::fifo)
        type_char = 'p';
    else if (type == fs::file_type::socket)
        type_char = 's';

    // Permissions string (rwxrwxrwx)
    auto perms = status.permissions();
    auto perm_char = [](fs::perms p, fs::perms check, char c) {
        return (p & check) != fs::perms::none ? c : '-';
    };

    std::string perm_str;
    perm_str += perm_char(perms, fs::perms::owner_read, 'r');
    perm_str += perm_char(perms, fs::perms::owner_write, 'w');
    perm_str += perm_char(perms, fs::perms::owner_exec, 'x');
    perm_str += perm_char(perms, fs::perms::group_read, 'r');
    perm_str += perm_char(perms, fs::perms::group_write, 'w');
    perm_str += perm_char(perms, fs::perms::group_exec, 'x');
    perm_str += perm_char(perms, fs::perms::others_read, 'r');
    perm_str += perm_char(perms, fs::perms::others_write, 'w');
    perm_str += perm_char(perms, fs::perms::others_exec, 'x');

    // File size
    std::string size_str = "   -";
    if (type == fs::file_type::regular) {
        size_str = format_size(fs::file_size(path, ec));
    }

    // Last modified time
    auto ftime = fs::last_write_time(path, ec);
    auto sctp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm *tm = std::localtime(&tt);

    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", tm);

    oss << type_char << perm_str << "  " << std::setw(6) << std::right
        << size_str << "  " << time_buf;

    return oss.str();
};

std::string to_lower(std::string_view str)
{
    std::string lower_case_string;
    lower_case_string.reserve(str.size());

    for (char c : str) {
        unsigned char lc = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(c)));
        lower_case_string.push_back(static_cast<char>(lc));
    }

    return lower_case_string;
}

PackedStrings::PackedStrings()
{
    data_.reserve(1024 * 1024);
    indices_.reserve(16384);
    // Enter 16 characters padding for SIMD operations searching backwards
    push("FFFFFFFFFFFFFFFF"); 
}

void PackedStrings::push(const std::string &str)
{
    indices_.push_back(data_.size());
    data_.insert(data_.end(), str.begin(), str.end());
    data_.push_back('\0');
}

void PackedStrings::merge(PackedStrings &&other)
{
    size_t data_offset = data_.size();

    // Append raw data_
    data_.insert(data_.end(), other.data_.begin(), other.data_.end());

    // Append indices, adjusted by offset
    indices_.reserve(indices_.size() + other.indices_.size());
    for (size_t idx : other.indices_) {
        indices_.push_back(idx + data_offset);
    }
}

std::string_view PackedStrings::at(size_t idx) const
{
    const char *ptr = data_.data() + indices_[idx];
    return std::string_view(ptr);
}

void PackedStrings::shrink_to_fit()
{
    data_.shrink_to_fit();
    indices_.shrink_to_fit();
}

bool PackedStrings::empty() const noexcept { return indices_.empty(); }
size_t PackedStrings::size() const noexcept { return indices_.size(); }

PackedStrings::iterator PackedStrings::begin() const
{
    return iterator(this, 0);
}

PackedStrings::iterator PackedStrings::end() const
{
    return iterator(this, indices_.size());
}

PackedStrings::iterator::iterator(const PackedStrings *container, size_t idx)
    : container_(container), idx_(idx)
{
}

std::string_view PackedStrings::iterator::operator*() const
{
    return container_->at(idx_);
}

std::string_view PackedStrings::iterator::operator[](difference_type n) const
{
    auto new_idx = static_cast<difference_type>(idx_) + n;
    return container_->at(static_cast<size_t>(new_idx));
}

PackedStrings::iterator &PackedStrings::iterator::operator++()
{
    ++idx_;
    return *this;
}
PackedStrings::iterator PackedStrings::iterator::operator++(int)
{
    iterator tmp = *this;
    ++idx_;
    return tmp;
}
PackedStrings::iterator &PackedStrings::iterator::operator--()
{
    --idx_;
    return *this;
}

PackedStrings::iterator PackedStrings::iterator::operator--(int)
{
    iterator tmp = *this;
    --idx_;
    return tmp;
}

PackedStrings::iterator &PackedStrings::iterator::operator+=(difference_type n)
{
    idx_ = static_cast<size_t>(static_cast<difference_type>(idx_) + n);
    return *this;
}

PackedStrings::iterator &PackedStrings::iterator::operator-=(difference_type n)
{
    idx_ = static_cast<size_t>(static_cast<difference_type>(idx_) - n);
    return *this;
}

PackedStrings::iterator
PackedStrings::iterator::operator+(difference_type n) const
{
    return {container_,
            static_cast<size_t>(static_cast<difference_type>(idx_) + n)};
}
PackedStrings::iterator
PackedStrings::iterator::operator-(difference_type n) const
{
    return {container_,
            static_cast<size_t>(static_cast<difference_type>(idx_) - n)};
}

PackedStrings::iterator::difference_type
PackedStrings::iterator::operator-(const iterator &other) const
{
    return static_cast<difference_type>(idx_) -
           static_cast<difference_type>(other.idx_);
}

bool PackedStrings::iterator::operator==(
    const PackedStrings::iterator &other) const
{
    return idx_ == other.idx_;
}
bool PackedStrings::iterator::operator!=(
    const PackedStrings::iterator &other) const
{
    return idx_ != other.idx_;
}
bool PackedStrings::iterator::operator<(
    const PackedStrings::iterator &other) const
{
    return idx_ < other.idx_;
}
bool PackedStrings::iterator::operator<=(
    const PackedStrings::iterator &other) const
{
    return idx_ <= other.idx_;
}
bool PackedStrings::iterator::operator>(
    const PackedStrings::iterator &other) const
{
    return idx_ > other.idx_;
}
bool PackedStrings::iterator::operator>=(
    const PackedStrings::iterator &other) const
{
    return idx_ >= other.idx_;
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

std::string path_to_string(const fs::path& path) {
#ifdef PLATFORM_WIN32
    const auto u8_filename = path.filename().u8string();
    return std::string(u8_filename.cbegin(), u8_filename.cend());
#else
    return path.filename().string();
#endif
}

std::optional<std::filesystem::path> get_home_dir() {
#ifdef PLATFORM_WIN32
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
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
#ifdef PLATFORM_WIN32
    const char *temp = std::getenv("TEMP");
    if (!temp) {
        temp = std::getenv("TMP");
    }
    if (!temp) {
        return fs::path("C:\\Temp");
    }
#else
    const char *temp = std::getenv("TMPDIR");
    if (!temp) {
        temp = "/tmp";
    }
#endif
    return fs::path(temp);
}


#ifdef PLATFORM_WIN32
void copy_to_clipboard(const std::string &content)
{
    if (!OpenClipboard(nullptr)) {
        throw std::runtime_error("Failed to open clipboard");
    }

    EmptyClipboard();

    // Allocate global memory for the string
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, content.size() + 1);
    if (!hMem) {
        CloseClipboard();
        throw std::runtime_error("Failed to allocate clipboard memory");
    }

    // Copy the string to the global memory
    memcpy(GlobalLock(hMem), content.c_str(), content.size() + 1);
    GlobalUnlock(hMem);

    // Set the clipboard data
    if (!SetClipboardData(CF_TEXT, hMem)) {
        GlobalFree(hMem);
        CloseClipboard();
        throw std::runtime_error("Failed to set clipboard data");
    }

    CloseClipboard();
}

void run_command(const std::vector<std::string> &args)
{
    if (args.empty()) {
        throw std::runtime_error("No command specified");
    }

    // Build command line string
    std::string cmdline;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0)
            cmdline += " ";

        // Quote arguments that contain spaces
        if (args[i].find(' ') != std::string::npos) {
            cmdline += "\"" + args[i] + "\"";
        } else {
            cmdline += args[i];
        }
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // Create detached process
    if (!CreateProcessA(nullptr,                             // Application name
                        const_cast<char *>(cmdline.c_str()), // Command line
                        nullptr,          // Process attributes
                        nullptr,          // Thread attributes
                        FALSE,            // Inherit handles
                        DETACHED_PROCESS, // Creation flags
                        nullptr,          // Environment
                        nullptr,          // Current directory
                        &si,              // Startup info
                        &pi))             // Process info
    {
        throw std::runtime_error("Failed to launch command: " + args[0]);
    }

    // Close handles immediately (detached process)
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void run_custom_command(const std::string &cmd,
                        const std::optional<fs::path> &path, bool stdout_to_clipboard)
{
    if (cmd.empty()) {
        throw std::runtime_error("Custom command is empty");
    }

    // Set environment variables if path is provided
    std::vector<std::pair<std::string, std::string>> old_env;
    if (path.has_value()) {
        const auto &_path = path.value();
        auto set_env = [&](const char *name, const std::string &value) {
            char old_value[MAX_PATH] = {};
            GetEnvironmentVariableA(name, old_value, MAX_PATH);
            old_env.push_back({name, old_value});
            SetEnvironmentVariableA(name, value.c_str());
        };

        set_env("FILEPATH", _path.string());
        set_env("FILENAME", _path.filename().string());
        set_env("PARENT_DIR", _path.parent_path().string());
        set_env("EXTENSION", _path.extension().string());
    }

    // Run with cmd.exe
    std::string cmdline = "cmd.exe /c " + cmd;

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    HANDLE stdout_read = nullptr, stdout_write = nullptr;

    if (stdout_to_clipboard) {
        // Create pipe for stdout capture
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
            throw std::runtime_error(
                "Failed to create pipe for command output");
        }

        SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = stdout_write;
        si.hStdError = stdout_write;
    }

    if (!CreateProcessA(nullptr, const_cast<char *>(cmdline.c_str()), nullptr,
                        nullptr,
                        TRUE, // Inherit handles for pipe
                        stdout_to_clipboard ? 0 : DETACHED_PROCESS, nullptr,
                        nullptr, &si, &pi)) {
        if (stdout_read)
            CloseHandle(stdout_read);
        if (stdout_write)
            CloseHandle(stdout_write);
        throw std::runtime_error("Custom command failed: " + cmd);
    }

    if (stdout_to_clipboard) {
        CloseHandle(stdout_write);

        // Read output
        std::string output;
        char buffer[4096];
        DWORD bytes_read;

        while (ReadFile(stdout_read, buffer, sizeof(buffer), &bytes_read,
                        nullptr) &&
               bytes_read > 0) {
            output.append(buffer, bytes_read);
        }

        CloseHandle(stdout_read);

        // Wait for process to finish
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exit_code;
        GetExitCodeProcess(pi.hProcess, &exit_code);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exit_code != 0) {
            throw std::runtime_error("Custom command failed: " + cmd);
        }

        // Remove trailing newline
        if (!output.empty() && output.back() == '\n') {
            output.pop_back();
        }
        if (!output.empty() && output.back() == '\r') {
            output.pop_back();
        }

        if (!output.empty()) {
            copy_to_clipboard(output);
        }
    } else {
        // Detached - close immediately
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Restore environment variables
    for (const auto &[name, value] : old_env) {
        if (value.empty()) {
            SetEnvironmentVariableA(name.c_str(), nullptr);
        } else {
            SetEnvironmentVariableA(name.c_str(), value.c_str());
        }
    }
}

#else
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

void run_custom_command_with_capture(const std::string &cmd, const std::optional<fs::path>& path)
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
                        const std::optional<fs::path> &path, bool stdout_to_clipboard)
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
#endif