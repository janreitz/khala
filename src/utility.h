#pragma once

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

std::string serialize_file_info(const std::filesystem::path& path);

std::string to_lower(std::string_view str);

std::string read_file(const std::filesystem::path &path);

// Platform specific helpers

std::string path_to_string(const std::filesystem::path &path);
std::optional<std::filesystem::path> get_home_dir();
std::filesystem::path get_temp_dir();

void copy_to_clipboard(const std::string &content);
void run_command(const std::vector<std::string> &args);
void run_custom_command(const std::string &cmd,
                        const std::optional<std::filesystem::path> &path,
                        bool stdout_to_clipboard);