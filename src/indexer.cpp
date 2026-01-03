#include "indexer.h"
#include "utility.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace indexer
{

PackedStrings scan_subtree(const fs::path &root,
                           const std::set<fs::path> &ignore_dirs,
                           const std::set<std::string> &ignore_dir_names)
{
    PackedStrings paths;
    try {
        const auto canon_root = fs::canonical(root);
        for (auto it = fs::recursive_directory_iterator(
                 canon_root, fs::directory_options::skip_permission_denied);
             it != fs::end(it); ++it) {

            if (it->is_directory()) {
                // Check both full paths and directory names
                if (ignore_dirs.contains(it->path()) ||
                    ignore_dir_names.contains(it->path().filename().string())) {
                    it.disable_recursion_pending();
                    continue;
                }
            }

            if (it->is_regular_file()) {
                paths.push(it->path().string());
            }
        }
    } catch (const fs::filesystem_error &) {
    }
    paths.shrink_to_fit();
    return paths;
}

PackedStrings
scan_filesystem_parallel(const fs::path &root_path,
                         const std::set<fs::path> &ignore_dirs,
                         const std::set<std::string> &ignore_dir_names)
{
    PackedStrings result;
    std::vector<fs::path> subdirs;

    // Collect top-level entries
    try {
        const auto canon_root = fs::canonical(root_path);
        for (const auto &entry : fs::directory_iterator(canon_root)) {
            if (entry.is_directory()) {
                // Check both full paths and directory names
                if (!ignore_dirs.contains(entry.path()) &&
                    !ignore_dir_names.contains(
                        entry.path().filename().string())) {
                    subdirs.push_back(entry.path());
                }
            } else if (entry.is_regular_file()) {
                result.push(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error &e) {
        fprintf(stderr, "Error reading root: %s\n", e.what());
        return result;
    }

    std::vector<std::future<PackedStrings>> futures;
    futures.reserve(subdirs.size());
    for (const auto &subdir : subdirs) {
        futures.push_back(std::async(std::launch::async, scan_subtree, subdir,
                                     ignore_dirs, ignore_dir_names));
    }

    for (auto &fut : futures) {
        result.merge(fut.get());
    }

    return result;
}

void scan_subtree_streaming(const fs::path &root,
                            const std::set<fs::path> &ignore_dirs,
                            const std::set<std::string> &ignore_dir_names,
                            StreamingIndex &index, size_t chunk_size)
{
    PackedStrings current_chunk;

    try {
        for (auto it = fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied);
             it != fs::end(it); ++it) {

            if (it->is_directory()) {
                // Check both full paths and directory names
                if (ignore_dirs.contains(it->path()) ||
                    ignore_dir_names.contains(it->path().filename().string())) {
                    it.disable_recursion_pending();
                    continue;
                }
            }

            // if (it->is_regular_file()) {
                current_chunk.push(it->path().string());

                if (current_chunk.size() >= chunk_size) {
                    current_chunk.shrink_to_fit();
                    index.add_chunk(std::move(current_chunk));
                    current_chunk = PackedStrings{};
                }
            // }
        }
    } catch (const fs::filesystem_error &) {
    }

    // Emit remaining files
    if (!current_chunk.empty()) {
        current_chunk.shrink_to_fit();
        index.add_chunk(std::move(current_chunk));
    }
}

void scan_filesystem_streaming(const fs::path &root_path, StreamingIndex &index,
                               const std::set<fs::path> &ignore_dirs,
                               const std::set<std::string> &ignore_dir_names,
                               size_t chunk_size)
{
    const defer mark_complete(
        [&index]() noexcept { index.mark_scan_complete(); });

    PackedStrings root_files;

    const auto min_work_units = std::thread::hardware_concurrency() * 4;
    std::deque<fs::path> to_expand;
    try {
        to_expand.push_back(fs::canonical(root_path));
    } catch (const fs::filesystem_error &e) {
        fprintf(stderr, "Error reading root: %s\n", e.what());
        return;
    }

    while (!to_expand.empty() && to_expand.size() < min_work_units) {
        const auto path = to_expand.front();
        to_expand.pop_front();

        try {
            for (const auto &entry : fs::directory_iterator(path)) {
                if (entry.is_directory()) {
                    if (ignore_dir_names.contains(
                            entry.path().filename().string()) ||
                        ignore_dirs.contains(entry.path())) {
                        continue;
                    }
                    to_expand.push_back(entry.path());
                } else if (entry.is_regular_file()) {
                    root_files.push(entry.path().string());
                }
            }
        } catch (const fs::filesystem_error &) {
            // TODO log errors but continue
        }
    }

    // Add root files as first chunk
    if (!root_files.empty()) {
        index.add_chunk(std::move(root_files));
    }

    std::vector<fs::path> work_units(to_expand.cbegin(), to_expand.cend());
    std::atomic<size_t> next_dir{0};
    std::vector<std::thread> workers;
    const size_t num_threads =
        std::min(work_units.size(),
                 static_cast<size_t>(std::thread::hardware_concurrency()));

    printf("Number of work units %ld, hardware_concurrency: %d, number of threads: %ld\n", work_units.size(), std::thread::hardware_concurrency(), num_threads);
    workers.reserve(num_threads);

    for (size_t i = 0; i < num_threads; i++) {
        workers.emplace_back([&]() {
            for (;;) {
                const size_t idx =
                    next_dir.fetch_add(1, std::memory_order_relaxed);
                if (idx >= work_units.size())
                    break;

                scan_subtree_streaming(work_units[idx], ignore_dirs,
                                       ignore_dir_names, index, chunk_size);
            }
        });
    }

    for (auto &w : workers) {
        w.join();
    }
}

std::unordered_map<std::string, std::string>
parse_desktop_file(const fs::path &desktop_file_path)
{
    std::unordered_map<std::string, std::string> entries;
    std::ifstream file(desktop_file_path);
    std::string line;
    bool in_desktop_entry_section = false;

    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Check for section headers
        if (line[0] == '[' && line.back() == ']') {
            in_desktop_entry_section = (line == "[Desktop Entry]");
            continue;
        }

        // Only parse entries in [Desktop Entry] section
        if (!in_desktop_entry_section) {
            continue;
        }

        // Parse key=value pairs
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            entries[key] = value;
        }
    }

    return entries;
}

std::vector<DesktopApp> scan_desktop_files()
{
    std::vector<DesktopApp> apps;

    // Standard desktop file locations
    std::vector<fs::path> search_paths = {
        "/usr/share/applications", "/usr/local/share/applications",
        fs::path(std::getenv("HOME") ? std::getenv("HOME") : "") /
            ".local/share/applications"};

    for (const auto &search_path : search_paths) {
        if (!fs::exists(search_path)) {
            continue;
        }

        try {
            for (const auto &entry : fs::directory_iterator(search_path)) {
                if (!entry.is_regular_file() ||
                    entry.path().extension() != ".desktop") {
                    continue;
                }

                auto desktop_entries = parse_desktop_file(entry.path());

                // Check if this is a valid application
                auto type_it = desktop_entries.find("Type");
                auto no_display_it = desktop_entries.find("NoDisplay");
                auto hidden_it = desktop_entries.find("Hidden");

                if (type_it == desktop_entries.end() ||
                    type_it->second != "Application") {
                    continue;
                }

                if ((no_display_it != desktop_entries.end() &&
                     no_display_it->second == "true") ||
                    (hidden_it != desktop_entries.end() &&
                     hidden_it->second == "true")) {
                    continue;
                }

                auto name_it = desktop_entries.find("Name");
                auto exec_it = desktop_entries.find("Exec");

                if (name_it == desktop_entries.end() ||
                    exec_it == desktop_entries.end()) {
                    continue;
                }

                std::string description;
                auto comment_it = desktop_entries.find("Comment");
                if (comment_it != desktop_entries.end()) {
                    description = comment_it->second;
                }

                // Clean up Exec command (remove %f %u etc. placeholders)
                std::string exec_command = exec_it->second;
                auto space_pos = exec_command.find(" %");
                if (space_pos != std::string::npos) {
                    exec_command = exec_command.substr(0, space_pos);
                }

                apps.push_back(DesktopApp{.name = name_it->second,
                                          .description = description,
                                          .exec_command = exec_command,
                                          .desktop_file_path = entry.path()});
            }
        } catch (const fs::filesystem_error &) {
            // Skip directories we can't read
            continue;
        }
    }

    return apps;
}

} // namespace indexer