#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace parallel {

// Executes a function in parallel over a range [begin, end)
// Uses static partitioning similar to OpenMP schedule(static)
// The function is called with each index in the range exactly once
template <typename Func>
void parallel_for(size_t begin, size_t end, Func &&func, size_t n_threads = std::thread::hardware_concurrency())
{
    if (begin >= end) {
        return;
    }

    const size_t total_work = end - begin;

    // Don't create more threads than work items
    const size_t actual_threads = std::min(n_threads, total_work);

    // Single-threaded fallback
    if (actual_threads <= 1) {
        for (size_t i = begin; i < end; ++i) {
            func(i);
        }
        return;
    }

    // Calculate chunk size for static partitioning
    const size_t chunk_size = total_work / actual_threads;
    const size_t remainder = total_work % actual_threads;

    std::vector<std::thread> threads;
    threads.reserve(actual_threads);

    for (size_t t = 0; t < actual_threads; ++t) {
        // Distribute remainder across first threads
        const size_t t_begin = begin + t * chunk_size + std::min(t, remainder);
        const size_t t_end =
            t_begin + chunk_size + (t < remainder ? 1 : 0);

        threads.emplace_back([t_begin, t_end, &func]() {
            for (size_t i = t_begin; i < t_end; ++i) {
                func(i);
            }
        });
    }

    for (auto &thread : threads) {
        thread.join();
    }
}

} // namespace parallel
