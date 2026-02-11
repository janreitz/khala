#include "indexer.h"
#include "logger.h"
#include "packed_strings.h"
#include "streamingindex.h"
#include "utility.h"

#include <algorithm>
#include <atomic>
#include <bits/fs_dir.h>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <future>
#include <set>
#include <string>
#include <thread>
#include <utility>
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
                    ignore_dir_names.contains(
                        platform::path_to_string(it->path().filename()))) {
                    it.disable_recursion_pending();
                    continue;
                }
            }

            if (it->is_regular_file() || it->is_directory()) {
                paths.push(platform::path_to_string(it->path()));
            }
        }
    } catch (const fs::filesystem_error &) {
    }
    paths.shrink_to_fit();
    return paths;
}

PackedStrings
scan_filesystem_parallel(const std::set<fs::path> &root_paths,
                         const std::set<fs::path> &ignore_dirs,
                         const std::set<std::string> &ignore_dir_names)
{
    PackedStrings result;
    std::vector<fs::path> subdirs;

    // Collect top-level entries from all roots
    for (const auto &root_path : root_paths) {
        try {
            const auto canon_root = fs::canonical(root_path);
            for (const auto &entry : fs::directory_iterator(canon_root)) {
                if (entry.is_directory()) {
                    // Check both full paths and directory names
                    if (!ignore_dirs.contains(entry.path()) &&
                        !ignore_dir_names.contains(platform::path_to_string(
                            entry.path().filename()))) {
                        subdirs.push_back(entry.path());
                    }
                } else if (entry.is_regular_file()) {
                    result.push(entry.path().string());
                }
            }
        } catch (const fs::filesystem_error &e) {
            LOG_ERROR("Error reading root %s: %s",
                      platform::path_to_string(root_path).c_str(), e.what());
        }
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
    current_chunk.reserve(chunk_size, platform::MAX_PATH_LENGTH);
    // Prefix for SIMD operations that scan backwards
    current_chunk.prefix(16, 'F');

    try {
        for (auto it = fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied);
             it != fs::end(it); ++it) {

            if (it->is_directory()) {
                // Check both full paths and directory names
                if (ignore_dirs.contains(it->path()) ||
                    ignore_dir_names.contains(
                        platform::path_to_string(it->path().filename()))) {
                    it.disable_recursion_pending();
                    continue;
                }
            }

            if (it->is_regular_file() || it->is_directory()) {
                current_chunk.push(platform::path_to_string(it->path()));

                if (current_chunk.size() >= chunk_size) {
                    index.add_chunk(std::move(current_chunk));
                    current_chunk = PackedStrings{};
                    current_chunk.prefix(16, 'F');
                }
            }
        }
    } catch (const fs::filesystem_error &e) {
        LOG_WARNING("Exception while indexing %s: %s",
                    platform::path_to_string(e.path1()).c_str(), e.what());
    }

    // Emit remaining files
    if (!current_chunk.empty()) {
        current_chunk.shrink_to_fit();
        index.add_chunk(std::move(current_chunk));
    }
}

void scan_filesystem_streaming(const std::set<fs::path> &root_paths,
                               StreamingIndex &index,
                               const std::set<fs::path> &ignore_dirs,
                               const std::set<std::string> &ignore_dir_names,
                               size_t chunk_size)
{
    const defer mark_complete(
        [&index]() noexcept { index.mark_scan_complete(); });

    const auto min_work_units = std::thread::hardware_concurrency() * 4;
    std::deque<fs::path> to_expand;

    PackedStrings root_files;
    root_files.reserve(min_work_units, platform::MAX_PATH_LENGTH);
    // Prefix for SIMD operations that scan backwards
    root_files.prefix(16, 'F');

    // Initialize with all root paths
    for (const auto &root_path : root_paths) {
        try {
            to_expand.push_back(fs::canonical(root_path));
        } catch (const fs::filesystem_error &e) {
            LOG_ERROR("Error reading root %s: %s",
                      platform::path_to_string(root_path).c_str(), e.what());
        }
    }

    if (to_expand.empty()) {
        LOG_ERROR("No valid index roots available");
        return;
    }

    while (!to_expand.empty() && to_expand.size() < min_work_units) {
        const auto path = to_expand.front();
        to_expand.pop_front();

        try {
            for (const auto &entry : fs::directory_iterator(path)) {
                if (entry.is_directory()) {
                    if (ignore_dir_names.contains(platform::path_to_string(
                            entry.path().filename())) ||
                        ignore_dirs.contains(entry.path())) {
                        continue;
                    }
                    to_expand.push_back(entry.path());
                } else if (entry.is_regular_file()) {
                    root_files.push(platform::path_to_string(entry.path()));
                }
            }
        } catch (const fs::filesystem_error &e) {
            LOG_WARNING("Exception while indexing %s: %s",
                        platform::path_to_string(e.path1()).c_str(), e.what());
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

    LOG_DEBUG("Number of work units %ld, hardware_concurrency: %d, number of "
              "threads: %ld",
              work_units.size(), std::thread::hardware_concurrency(),
              num_threads);
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
} // namespace indexer