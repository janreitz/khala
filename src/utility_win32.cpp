#include "utility.h"

#include <filesystem>
#include <optional>

#include <Windows.h>

namespace fs = std::filesystem;

namespace platform
{
std::string path_to_string(const fs::path &path)
{
    const auto u8_filename = path.u8string();
    return {u8_filename.cbegin(), u8_filename.cend()};
}

std::optional<std::filesystem::path> get_home_dir()
{
    const char *home = std::getenv("USERPROFILE");

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
    const char *temp = std::getenv("TEMP");
    if (!temp) {
        temp = std::getenv("TMP");
    }
    if (!temp) {
        return fs::path("C:\\Temp");
    }

    return fs::path(temp);
}

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
                        const std::optional<fs::path> &path,
                        bool stdout_to_clipboard)
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

void open_file(const fs::path &path)
{
    const std::string path_str = platform::path_to_string(path);
    HINSTANCE result = ShellExecuteA(nullptr,          // Parent window
                                     "open",           // Operation
                                     path_str.c_str(), // File to open
                                     nullptr,          // Parameters
                                     nullptr,          // Working directory
                                     SW_SHOWNORMAL     // Show command
    );

    if ((INT_PTR)result <= 32) {
        throw std::runtime_error("Failed to open file: " + path_str);
    }
}

void open_directory(const fs::path &path)
{
    const std::string path_str = path_to_string(path);
    HINSTANCE result = ShellExecuteA(
        nullptr,
        "explore", // Use "explore" for directories to open in Explorer
        path_str.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    if ((INT_PTR)result <= 32) {
        throw std::runtime_error("Failed to open directory: " + path_str);
    }
}

// === Autostart Implementation ===

bool setup_autostart(bool enable)
{
    HKEY hKey;
    const char *run_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    if (RegOpenKeyExA(HKEY_CURRENT_USER, run_key, 0,
                      KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    bool success = false;
    if (enable) {
        // Get current executable path
        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);

        success = RegSetValueExA(hKey, "Khala", 0, REG_SZ,
                                 reinterpret_cast<BYTE *>(exe_path),
                                 static_cast<DWORD>(strlen(exe_path) + 1)) ==
                  ERROR_SUCCESS;
    } else {
        LONG result = RegDeleteValueA(hKey, "Khala");
        success = (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    }

    RegCloseKey(hKey);
    return success;
}

bool is_autostart_enabled()
{
    HKEY hKey;
    const char *run_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    if (RegOpenKeyExA(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &hKey) !=
        ERROR_SUCCESS) {
        return false;
    }

    DWORD type;
    DWORD size = 0;
    bool exists =
        RegQueryValueExA(hKey, "Khala", NULL, &type, NULL, &size) == ERROR_SUCCESS;

    RegCloseKey(hKey);
    return exists;
}
} // namespace platform