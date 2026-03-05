#include "ranker.h"
#include "fuzzy.h"
#include "lastwriterwinsslot.h"
#include "logger.h"
#include "parallel.h"
#include "streamingindex.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

StreamingRanker::StreamingRanker(StreamingIndex &index,
                                 LastWriterWinsSlot<ResultUpdate> &results)
    : streaming_index_(index), result_updates_(results),
      worker_thread_([this]() { run(); })
{
}

StreamingRanker::~StreamingRanker()
{
    should_exit_.store(true, std::memory_order_release);
    state_cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void StreamingRanker::pause()
{
    active_.store(false, std::memory_order_release);
    state_cv_.notify_one();
}

void StreamingRanker::resume()
{
    active_.store(true, std::memory_order_release);
    query_changed_.store(true, std::memory_order_release);
    state_cv_.notify_one();
}

void StreamingRanker::update_query(std::string query)
{
    const std::lock_guard lock(state_mutex_);
    ranker_request_.query = std::move(query);
    query_changed_.store(true, std::memory_order_release);
    state_cv_.notify_one();
}

void StreamingRanker::update_requested_count(size_t count)
{
    const std::lock_guard lock(state_mutex_);
    ranker_request_.requested_count = count;
    query_changed_.store(true, std::memory_order_release);
    state_cv_.notify_one();
}

void StreamingRanker::update_request(std::string query, size_t count)
{
    const std::lock_guard lock(state_mutex_);
    ranker_request_.query = std::move(query);
    ranker_request_.requested_count = count;
    query_changed_.store(true, std::memory_order_release);
    state_cv_.notify_one();
}

void StreamingRanker::run()
{
    while (!should_exit_.load(std::memory_order_relaxed)) {
        // Wait when inactive
        {
            std::unique_lock lock(state_mutex_);
            state_cv_.wait(lock, [this]() {
                return active_.load(std::memory_order_acquire) ||
                       should_exit_.load(std::memory_order_acquire);
            });
        }

        if (should_exit_.load(std::memory_order_acquire)) {
            break;
        }

        // Check for query or request changes
        bool only_count_increased = false;
        if (query_changed_.exchange(false, std::memory_order_acq_rel)) {
            RankerRequest new_request;
            {
                const std::lock_guard lock(state_mutex_);
                new_request = ranker_request_;
            }

            // Reset if query changed, otherwise just update count
            if (current_request_.query != new_request.query) {
                reset_state();
            } else if (new_request.requested_count >
                       current_request_.requested_count) {
                const bool heap_was_full =
                    top_results_.size() >= RANKING_HEAP_CAPACITY;
                if (new_request.requested_count > top_results_.size() &&
                    heap_was_full) {
                    // Scrolled past precomputed buffer — re-score with higher
                    // cap
                    reset_state();
                } else {
                    only_count_increased = true;
                }
            }
            current_request_ = new_request;
        }

        // Special case: count increased but no new chunks - re-sort existing
        // scored chunks
        if (only_count_increased &&
            processed_chunks_ == streaming_index_.get_available_chunks()) {
            handle_count_increase();
            continue;
        }

        // Sleep and loop back to check for query changes or new chunks
        const auto available_chunks = streaming_index_.get_available_chunks();
        if (processed_chunks_ == available_chunks &&
            !streaming_index_.is_scan_complete()) {
            streaming_index_.wait_for_new_chunks(processed_chunks_);
            continue;
        }

        // Process available chunks
        process_chunks();

        // Final update when scan completes
        if (streaming_index_.is_scan_complete() &&
            processed_chunks_ == streaming_index_.get_available_chunks()) {

            send_update(true);

            // Wait for next query or state change
            std::unique_lock lock(state_mutex_);
            state_cv_.wait(lock, [this]() {
                return !active_.load(std::memory_order_acquire) ||
                       query_changed_.load(std::memory_order_acquire) ||
                       should_exit_.load(std::memory_order_acquire);
            });
        }
    }
}

void StreamingRanker::reset_state()
{
    processed_chunks_ = 0;
    total_result_count_ = 0;
    accumulated_results_.clear();
    top_results_.clear();
}

void StreamingRanker::handle_count_increase()
{
    // Just rebuild accumulated results with new count
    report_results();
}

void StreamingRanker::process_chunks()
{
    const size_t available_chunks = streaming_index_.get_available_chunks();
    if (processed_chunks_ >= available_chunks) {
        return;
    }

    const size_t chunks_to_process = available_chunks - processed_chunks_;
    size_t processed_string_count = 0;

    // Skip scoring if query is empty, but still update metadata
    if (!current_request_.query.empty()) {
        const auto start_time = std::chrono::steady_clock::now();
        // Each thread gets its own results vector
        std::vector<std::vector<StreamingRankResult>> thread_local_results(chunks_to_process);

        parallel::parallel_for(
            processed_chunks_, available_chunks, [&](size_t chunk_idx) {
                auto chunk = streaming_index_.get_chunk(chunk_idx);
                assert(chunk);
                const auto chunk_size = chunk->size();
                auto &local_results =
                    thread_local_results[chunk_idx - processed_chunks_];
                local_results.reserve(chunk_size /
                                      4); // estimate ~25% match rate

                for (uint16_t i = 0; i < chunk_size; ++i) {
                    const auto score = fuzzy::fuzzy_score_5_simd(
                        chunk->at(i), current_request_.query);

                    if (score > 0.0F) {
                        local_results.push_back(StreamingRankResult{
                            .chunk_idx = static_cast<uint16_t>(chunk_idx),
                            .local_idx = i,
                            .score = score,
                        });
                    }
                }
            });

        // Sequential merge
        const size_t effective_cap =
            std::max(RANKING_HEAP_CAPACITY, current_request_.requested_count);
        for (auto &thread_local_result : thread_local_results) {
            processed_string_count += thread_local_result.size();
            for (auto result : thread_local_result) {
                constexpr auto MinHeapCmp = std::greater<StreamingRankResult>{};
                if (top_results_.size() < effective_cap) {
                    top_results_.push_back(result);
                    std::push_heap(top_results_.begin(), top_results_.end(),
                                   MinHeapCmp);
                } else if (result.score > top_results_.front().score) {
                    std::pop_heap(top_results_.begin(), top_results_.end(),
                                  MinHeapCmp);
                    top_results_.back() =
                        result; // Overwrite lowest scoring RankResult
                    std::push_heap(top_results_.begin(), top_results_.end(),
                                   MinHeapCmp);
                }
            }
        }

        total_result_count_ += processed_string_count;
        const auto end_time = std::chrono::steady_clock::now();
        const auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
               
         start_time);

        LOG_DEBUG("Scored %zu strings in %.2ldms (query: '%s', chunks: %zu)",
                  processed_string_count, duration.count(),
                  current_request_.query.c_str(), chunks_to_process);
    }

    // Update processed chunks count
    processed_chunks_ = available_chunks;

    // Report results once after processing all available chunks
    report_results();
}

void StreamingRanker::report_results()
{
    const size_t n =
        std::min(current_request_.requested_count, top_results_.size());

    // Sort all scored results to get top requested_count
    auto copy_to_sort = top_results_;
    std::partial_sort(copy_to_sort.begin(),
                      copy_to_sort.begin() + static_cast<std::ptrdiff_t>(n),
                      copy_to_sort.end(), [](const auto &a, const auto &b) {
                          return a.score > b.score;
                      });
    copy_to_sort.resize(n);

    // Convert top n to FileResult
    accumulated_results_.clear();
    accumulated_results_.reserve(n);

    for (const auto rank_result : copy_to_sort) {
        // Find the file path from chunk and global index
        auto chunk = streaming_index_.get_chunk(rank_result.chunk_idx);
        assert(chunk);
        assert(rank_result.local_idx < chunk->size());
        accumulated_results_.push_back(
            FileResult{.path = std::string(chunk->at(rank_result.local_idx)),
                       .score = rank_result.score});
    }
    send_update();
}

void StreamingRanker::send_update(bool is_final)
{
    ResultUpdate update;
    update.results = accumulated_results_;
    update.scan_complete =
        is_final ? true : streaming_index_.is_scan_complete();
    update.total_files = streaming_index_.get_total_files();
    update.processed_chunks = processed_chunks_;
    update.total_available_results = total_result_count_;

    result_updates_.write(std::move(update));
}