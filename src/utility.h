#pragma once

#include "packed_strings.h"
#include "types.h"

#include <filesystem>
#include <string>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

template <typename C>
requires std::is_nothrow_invocable_v<std::decay_t<C>>
struct defer {
    explicit defer(C &&callable) : _callable(std::move(callable)) {};
    defer(const defer &other) = delete;
    defer &operator=(const defer &other) = delete;
    defer(defer &&other) = delete;
    defer &operator=(defer &&other) = delete;
    ~defer() { _callable(); };

  private:
    C _callable;
};

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

std::string to_string(const ui::KeyboardEvent &hotkey);

std::string serialize_file_info(const std::filesystem::path& path);

std::string to_lower(std::string_view str);

std::string read_file(const std::filesystem::path &path);


int find_last_or(std::string_view str, char c, int _default);

int count_leading_zeros(unsigned int x);

// This requires up to sizeof(__m128i) before str.data();
int simd_find_last_or(std::string_view str, char c, int _default);

// Returns index of first occurrence of c at or after start, or -1 if not found
int simd_find_first_or(const char *data, size_t len, char c, size_t start,
                       int _default);

void simd_to_lower(const char *src, size_t len, char *out_buffer);

// Returns positions of all matches (up to max_results)
size_t find_all(const char *data, size_t len, char target, size_t *positions,
                size_t max_results);

// Returns positions of all matches (up to max_results)
size_t simd_find_all(const char *data, size_t len, char target,
                     size_t *positions, size_t max_results);

std::optional<std::filesystem::path> get_dir(std::string_view path);

struct ApplicationInfo {
    std::string name;
    std::string description;
    std::string exec_command;
    std::filesystem::path app_info_path;
};

// Platform specific helpers
namespace platform
{
extern const size_t MAX_PATH_LENGTH;

std::string path_to_string(const std::filesystem::path &path);
std::optional<std::filesystem::path> get_home_dir();
std::filesystem::path get_temp_dir();
std::filesystem::path get_data_dir();
std::filesystem::path get_history_path();

void copy_to_clipboard(const std::string &content);
void run_command(const std::vector<std::string> &args);
void run_custom_command(const std::string &cmd,
                        const std::optional<std::filesystem::path> &path,
                        bool stdout_to_clipboard,
                        const std::string &shell);
void open_file(const std::filesystem::path &path);
void open_directory(const std::filesystem::path &path);

std::vector<ApplicationInfo> scan_app_infos();

// Registers/unregisters app to start on system boot
// Windows: HKCU\Software\Microsoft\Windows\CurrentVersion\Run
// Linux: ~/.config/autostart/khala.desktop
bool setup_autostart(bool enable);
bool is_autostart_enabled();
} // namespace platform

// History file operations
void load_history(PackedStrings& history);
void save_history(const PackedStrings& history);