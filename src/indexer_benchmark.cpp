#include "config.h"
#include "fuzzy.h"
#include "indexer.h"
#include "parallel.h"
#include "ranker.h"

#include <algorithm>
#include <bits/chrono.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <map>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef HAVE_TBB
#include <execution>
#endif

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// Map of scoring algorithm names to function pointers (using PreparedQuery)
const std::map<std::string,
               std::function<float(std::string_view, std::string_view)>>
    scoring_algorithms = {
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
    const auto [config, warnings] = load_config(Config::default_path());
    printf("================ Indexing Benchmarks =================\n");
    try {
        printf("  Roots: %zu\n", config.index_roots.size());
        printf("================ Batch Approach =================\n");
        auto batch_start = std::chrono::steady_clock::now();
        auto paths = indexer::scan_filesystem_parallel(
            config.index_roots, config.ignore_dirs, config.ignore_dir_names);
        auto scan_end = std::chrono::steady_clock::now();
        auto scan_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(scan_end -
                                                                  batch_start);

        printf("=================================\n");
        printf("Batch indexing complete!\n");
        printf("  Filesystem scan (%zu entries): %zdms\n", paths.size(),
               scan_duration.count());

        printf("\n================ Streaming Approach =================\n");
        const auto streaming_start = std::chrono::steady_clock::now();

        StreamingIndex stream_index;
        indexer::scan_filesystem_streaming(config.index_roots, stream_index,
                                           config.ignore_dirs,
                                           config.ignore_dir_names, 1000);
        while (!stream_index.is_scan_complete()) {
            std::this_thread::sleep_for(10ms);
        }
        const auto streaming_scan_complete = std::chrono::steady_clock::now();
        auto streaming_scan_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                streaming_scan_complete - streaming_start);

        printf("=================================\n");
        printf("Streaming indexing complete!\n");
        printf("  Filesystem scan (%zu entries): %zdms\n",
               stream_index.get_total_files(), streaming_scan_duration.count());
        printf("  Total streaming time: %zdms\n",
               streaming_scan_duration.count());

        // ================ FUZZY SCORING BENCHMARKS =================
        printf(
            "\n================ Fuzzy Scoring Benchmark =================\n");
        printf("Using batch-scanned filesystem data (%zu entries) for scoring "
               "tests\n",
               paths.size());

        const std::vector<std::string> test_queries = {"main", "src", "config",
                                                       "test", "index"};

        for (const auto &test_query : test_queries) {
            printf("\n--- Testing with query: '%s' ---\n", test_query.c_str());

            // Test all scoring functions with prepared query
            for (const auto &[algo_name, scoring_func] : scoring_algorithms) {
                auto score_start = std::chrono::steady_clock::now();

                size_t scored_paths = 0;
                for (const auto &path : paths) {
                    float score = scoring_func(path, test_query);
                    if (score > 0.0F) {
                        scored_paths++;
                    }
                }

                auto score_end = std::chrono::steady_clock::now();
                auto score_duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        score_end - score_start);

                printf("  %s: %zdms (%zu paths scored, %zu matches)\n",
                       algo_name.c_str(), score_duration.count(), paths.size(),
                       scored_paths);
            }
        }

        // ================ PARALLEL SCORING BENCHMARKS =================
        printf("\n================ Parallel Scoring Benchmark "
               "=================\n");
        printf("Comparing different parallel approaches using "
               "fuzzy_score_5_simd\n");
        printf("Dataset size: %zu entries\n", paths.size());
        printf("Hardware threads: %u\n\n", std::thread::hardware_concurrency());

        for (const auto &test_query : test_queries) {
            printf("--- Query: '%s' ---\n", test_query.c_str());

            // Sequential baseline
            auto seq_start = std::chrono::steady_clock::now();
            std::vector<RankResult> sequential_results(paths.size());
            for (size_t i = 0; i < paths.size(); ++i) {
                const auto score =
                    fuzzy::fuzzy_score_5_simd(paths.at(i), test_query);
                sequential_results[i] = RankResult{i, score};
            }
            auto seq_end = std::chrono::steady_clock::now();
            auto seq_duration =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    seq_end - seq_start);

            printf("  Sequential:        %6.2fms  (baseline)\n",
                   static_cast<double>(seq_duration.count()) / 1000.0);

            // Custom parallel_for implementation
            auto custom_start = std::chrono::steady_clock::now();
            std::vector<RankResult> custom_results(paths.size());
            parallel::parallel_for(0, paths.size(), [&](size_t i) {
                const auto score =
                    fuzzy::fuzzy_score_5_simd(paths.at(i), test_query);
                custom_results[i] = RankResult{i, score};
            });
            auto custom_end = std::chrono::steady_clock::now();
            auto custom_duration =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    custom_end - custom_start);

            double custom_speedup =
                static_cast<double>(seq_duration.count()) /
                static_cast<double>(custom_duration.count());

            printf("  Custom parallel:   %6.2fms  (%.2fx speedup)\n",
                   static_cast<double>(custom_duration.count()) / 1000.0,
                   custom_speedup);

#ifdef _OPENMP
            // OpenMP parallel for
            auto omp_start = std::chrono::steady_clock::now();
            std::vector<RankResult> omp_results(paths.size());
#pragma omp parallel for schedule(static)
            for (int64_t i = 0; i < static_cast<int64_t>(paths.size()); ++i) {
                const auto score = fuzzy::fuzzy_score_5_simd(
                    paths.at(static_cast<size_t>(i)), test_query);
                omp_results[static_cast<size_t>(i)] =
                    RankResult{static_cast<size_t>(i), score};
            }
            auto omp_end = std::chrono::steady_clock::now();
            auto omp_duration =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    omp_end - omp_start);

            double omp_speedup = static_cast<double>(seq_duration.count()) /
                                 static_cast<double>(omp_duration.count());

            printf("  OpenMP:            %6.2fms  (%.2fx speedup)\n",
                   static_cast<double>(omp_duration.count()) / 1000.0,
                   omp_speedup);
#else
            printf("  OpenMP:            [not compiled with OpenMP support]\n");
#endif

#ifdef HAVE_TBB
            // std::execution parallel transform
            auto exec_start = std::chrono::steady_clock::now();
            std::vector<size_t> indices(paths.size());
            std::iota(indices.begin(), indices.end(), 0);
            std::vector<RankResult> exec_results(paths.size());

            std::transform(std::execution::par, indices.begin(), indices.end(),
                           exec_results.begin(), [&](size_t i) {
                               const auto score = fuzzy::fuzzy_score_5_simd(
                                   paths.at(i), test_query);
                               return RankResult{i, score};
                           });
            auto exec_end = std::chrono::steady_clock::now();
            auto exec_duration =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    exec_end - exec_start);

            double exec_speedup = static_cast<double>(seq_duration.count()) /
                                  static_cast<double>(exec_duration.count());

            printf("  std::execution:    %6.2fms  (%.2fx speedup)\n",
                   static_cast<double>(exec_duration.count()) / 1000.0,
                   exec_speedup);
#else
            printf("  std::execution:    [not compiled with TBB support]\n");
#endif

            printf("\n");
        }

    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    printf("\n================ Benchmarks Complete =================\n");
    return 0;
}
