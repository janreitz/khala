#include "indexer.h"
#include "utility.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace indexer
{

PackedStrings scan_subtree(const fs::path &root,
                           const std::set<fs::path> &ignore_dirs)
{
    PackedStrings paths;
    try {
        const auto canon_root = fs::canonical(root);
        for (auto it = fs::recursive_directory_iterator(
                 canon_root, fs::directory_options::skip_permission_denied); it != fs::end(it); ++it) {

            if (it->is_directory() && ignore_dirs.contains(it->path())) {
                it.disable_recursion_pending();
                continue;
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

PackedStrings scan_filesystem_parallel(const fs::path &root_path,
                                       const std::set<fs::path> &ignore_dirs)
{
    PackedStrings result;
    std::vector<fs::path> subdirs;

    // Collect top-level entries
    try {
        const auto canon_root = fs::canonical(root_path);
        for (const auto &entry : fs::directory_iterator(canon_root)) {
            if (!ignore_dirs.contains(entry.path()) && entry.is_directory()) {
                subdirs.push_back(entry.path());
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
        futures.push_back(
            std::async(std::launch::async, scan_subtree, subdir, ignore_dirs));
    }

    for (auto &fut : futures) {
        result.merge(fut.get());
    }

    return result;
}

void scan_filesystem_streaming(const fs::path &root_path,
                               StreamingIndex &index,
                               const std::set<fs::path> &ignore_dirs)
{
    std::vector<fs::path> subdirs;
    PackedStrings root_files;

    // Collect top-level entries
    try {
        const auto canon_root = fs::canonical(root_path);
        for (const auto &entry : fs::directory_iterator(canon_root)) {
            if (!ignore_dirs.contains(entry.path()) && entry.is_directory()) {
                subdirs.push_back(entry.path());
            } else if (entry.is_regular_file()) {
                root_files.push(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error &e) {
        fprintf(stderr, "Error reading root: %s\n", e.what());
        index.mark_scan_complete();
        return;
    }

    // Add root files as first chunk
    if (!root_files.empty()) {
        index.add_chunk(std::move(root_files));
    }

    // Process subdirectories concurrently, emitting chunks as they complete
    std::vector<std::future<void>> futures;
    futures.reserve(subdirs.size());
    
    for (const auto &subdir : subdirs) {
        futures.push_back(std::async(std::launch::async, [&index, subdir, &ignore_dirs]() {
            auto chunk = scan_subtree(subdir, ignore_dirs);
            if (!chunk.empty()) {
                index.add_chunk(std::move(chunk));
            }
        }));
    }

    // Wait for all tasks to complete
    for (auto &fut : futures) {
        fut.get();
    }

    index.mark_scan_complete();
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