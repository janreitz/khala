#include "fuzzy.h"
#include "indexer.h"
#include "ranker.h"
#include "utility.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// Map of scoring algorithm names to function pointers (using PreparedQuery)
const std::map<std::string, std::function<float(std::string_view, std::string_view)>> scoring_algorithms = {
    {"fuzzy_score", fuzzy::fuzzy_score},
    {"fuzzy_score_2", fuzzy::fuzzy_score_2},
    {"fuzzy_score_3", fuzzy::fuzzy_score_3},
    {"fuzzy_score_4", fuzzy::fuzzy_score_4},
    {"fuzzy_score_5", fuzzy::fuzzy_score_5},
    {"fuzzy_score_5_simd", fuzzy::fuzzy_score_5_simd},
};

int main()
{
    // Get root path from args or use home directory
    const Config config = Config::load(Config::default_path());
    printf("================ Indexing Benchmarks =================\n");
    try {
        printf("  Root: %s\n", config.index_root.string().c_str());
        printf("================ Batch Approach =================\n");
        auto batch_start = std::chrono::steady_clock::now();
        auto paths = indexer::scan_filesystem_parallel(config.index_root, config.ignore_dirs, config.ignore_dir_names);
        auto scan_end = std::chrono::steady_clock::now();
        auto scan_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(scan_end - batch_start);

        auto desktop_start = std::chrono::steady_clock::now();
        const auto desktop_apps = indexer::scan_desktop_files();
        auto scan_desktop_end = std::chrono::steady_clock::now();
        auto scan_desktop_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                scan_desktop_end - desktop_start);

        auto batch_total_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                scan_desktop_end - batch_start);

        printf("=================================\n");
        printf("Batch indexing complete!\n");
        printf("  Filesystem scan (%zu entries): %ldms\n", paths.size(), scan_duration.count());
        printf("  Desktop files scan: %ldms\n", scan_desktop_duration.count());
        printf("  Total batch time: %ldms\n", batch_total_duration.count());

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
        printf("Streaming indexing complete!\n");
        printf("  Filesystem scan (%ld entries): %ldms\n", stream_index.get_total_files(), streaming_scan_duration.count());
        printf("  Total streaming time: %ldms\n", streaming_scan_duration.count());
        
        // ================ FUZZY SCORING BENCHMARKS =================
        printf("\n================ Fuzzy Scoring Benchmark =================\n");
        printf("Using batch-scanned filesystem data (%zu entries) for scoring tests\n", paths.size());
        
        const std::vector<std::string> test_queries = {"main", "src", "config", "test", "index"};
        
        for (const auto& test_query : test_queries) {
            printf("\n--- Testing with query: '%s' ---\n", test_query.c_str());
            
            // Test all scoring functions with prepared query
            for (const auto& [algo_name, scoring_func] : scoring_algorithms) {
                auto score_start = std::chrono::steady_clock::now();
                
                size_t scored_paths = 0;
                for (const auto& path : paths) {
                    float score = scoring_func(path, test_query);
                    if (score > 0.0f) {
                        scored_paths++;
                    }
                }
                
                auto score_end = std::chrono::steady_clock::now();
                auto score_duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(score_end - score_start);
                
                printf("  %s: %ldms (%zu paths scored, %zu matches)\n", 
                       algo_name.c_str(), score_duration.count(), paths.size(), scored_paths);
            }
        }


    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    printf("\nIndexing finished!\n");
    return 0;
}
