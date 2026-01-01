#include "fuzzy.h"
#include "indexer.h"
#include "ranker.h"
#include "utility.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

int main()
{
    // Get root path from args or use home directory
    const Config config = Config::load(Config::default_path());

    printf("=======================\n\n");
    printf("Khala Indexer Benchmark\n");

    try {
        printf("  Root: %s\n", config.index_root.string().c_str());
        printf("================ Batch Approach =================\n");
        auto batch_start = std::chrono::steady_clock::now();
        auto paths = indexer::scan_filesystem_parallel(config.index_root, config.ignore_dirs, config.ignore_dir_names);
        auto scan_end = std::chrono::steady_clock::now();
        auto scan_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(scan_end -
                                                                  batch_start);
        auto ranked = rank_parallel(
            paths,
            [](std::string_view path) {
                return fuzzy::fuzzy_score(path, "query");
            },
            20);

        auto rank_end = std::chrono::steady_clock::now();
        auto rank_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(rank_end -
                                                                  scan_end);

        const auto desktop_apps = indexer::scan_desktop_files();
        auto scan_desktop_end = std::chrono::steady_clock::now();
        auto scan_desktop_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                scan_desktop_end - rank_end);

        auto total_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                scan_desktop_end - batch_start);

        printf("=================================\n");
        printf("Indexing complete! Scan filesystem time (%ld entries): %ldms "
               "Rank time %ldms Scan Desktop files time: %ldms  Total "
               "time: %ldms\n",
               paths.size(), scan_duration.count(), rank_duration.count(),
               scan_desktop_duration.count(), total_duration.count());

        printf("\n================ Streaming Approach =================\n");
        const auto streaming_start = std::chrono::steady_clock::now();

        StreamingIndex stream_index;
        indexer::scan_filesystem_streaming(config.index_root, stream_index, config.ignore_dirs, config.ignore_dir_names,
                                           1000);
        while (!stream_index.is_scan_complete()) {
            std::this_thread::sleep_for(10ms);
        }
        const auto streaming_scan_complete = std::chrono::steady_clock::now();
        auto streaming_scan_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                streaming_scan_complete - streaming_start);

        printf("=================================\n");
        printf("Indexing complete! Streaming scan filesystem time (%ld entries): %ldms Total "
               "time: %ldms\n",
               stream_index.get_total_files(), streaming_scan_duration.count(), streaming_scan_duration.count());


    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    printf("\nIndexing finished!\n");
    return 0;
}
