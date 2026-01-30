#include "ranker.h"
#include "fuzzy.h"
#include "lastwriterwinsslot.h"
#include "logger.h"
#include "parallel.h"
#include "streamingindex.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>

// TODO we could merge into the existing results without creating a copy?
std::vector<FileResult>
merge_top_results(const std::vector<FileResult> &existing,
                  const std::vector<FileResult> &new_results,
                  size_t max_results)
{
    std::vector<FileResult> merged;
    merged.reserve(existing.size() + new_results.size());

    // Merge sorted results
    auto it1 = existing.begin(), end1 = existing.end();
    auto it2 = new_results.begin(), end2 = new_results.end();

    while (merged.size() < max_results && (it1 != end1 || it2 != end2)) {
        if (it1 == end1) {
            merged.push_back(*it2++);
        } else if (it2 == end2) {
            merged.push_back(*it1++);
        } else if (it1->score >= it2->score) {
            merged.push_back(*it1++);
        } else {
            merged.push_back(*it2++);
        }
    }

    return merged;
}

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

void StreamingRanker::update_query(const std::string &query)
{
    std::lock_guard lock(state_mutex_);
    ranker_request_.query = query;
    query_changed_.store(true, std::memory_order_release);
    state_cv_.notify_one();
}

void StreamingRanker::update_requested_count(size_t count)
{
    std::lock_guard lock(state_mutex_);
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
                std::lock_guard lock(state_mutex_);
                new_request = ranker_request_;
            }

            // Reset if query changed, otherwise just update count
            if (current_request_.query != new_request.query) {
                reset_state();
            } else if (new_request.requested_count >
                       current_request_.requested_count) {
                // Only count increased - can reuse scored chunks
                only_count_increased = true;
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
        const auto available = streaming_index_.get_available_chunks();
        if (processed_chunks_ == available &&
            !streaming_index_.is_scan_complete()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
    global_offset_ = 0;
    accumulated_results_.clear();
    scored_results_.clear();
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

    // Collect chunk information for parallel processing
    struct ChunkInfo {
        size_t chunk_idx;
        size_t global_offset; // Offset into the complete file list
        size_t result_offset; // Offset into the results array for this batch
        size_t size;
    };

    std::vector<ChunkInfo> chunks_to_process;
    size_t result_offset = 0;

    for (size_t i = processed_chunks_; i < available_chunks; ++i) {
        const auto chunk = streaming_index_.get_chunk(i);
        assert(chunk);
        const size_t chunk_size = chunk->size();

        chunks_to_process.push_back(ChunkInfo{.chunk_idx = i,
                                              .global_offset = global_offset_,
                                              .result_offset = result_offset,
                                              .size = chunk_size});

        global_offset_ += chunk_size;
        result_offset += chunk_size;
    }

    // Skip scoring if query is empty, but still update metadata
    if (!current_request_.query.empty()) {
        // Total strings is the final result_offset value (accumulated size)
        const size_t total_strings = result_offset;

        const auto start_time = std::chrono::steady_clock::now();

        // Each thread gets its own results vector
        std::vector<std::vector<RankResult>> per_thread_results(chunks_to_process.size());

        parallel::parallel_for(
            0, chunks_to_process.size(), [&](size_t work_idx) {
                const auto &info = chunks_to_process[work_idx];
                auto chunk = streaming_index_.get_chunk(info.chunk_idx);
                assert(chunk);

                auto &local_results = per_thread_results[work_idx];
                local_results.reserve(info.size / 4);  // estimate ~25% match rate

                for (size_t i = 0; i < info.size; ++i) {
                    const auto score = fuzzy::fuzzy_score_5_simd(
                        chunk->at(i), current_request_.query);

                    if (score > 0.0F) {
                        local_results.push_back(
                            RankResult{info.global_offset + i, score});
                    }
                }
            });

        // Sequential merge
        for (auto &local : per_thread_results) {
            scored_results_.insert(scored_results_.end(), 
                                local.begin(), local.end());
        }

        const auto end_time = std::chrono::steady_clock::now();
        const auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                                  start_time);

        LOG_DEBUG("Scored %zu strings in %.2ldms (query: '%s', chunks: %zu)",
                  total_strings, duration.count(),
                  current_request_.query.c_str(), chunks_to_process.size());
    }

    // Update processed chunks count
    processed_chunks_ += chunks_to_process.size();

    // Report results once after processing all available chunks
    report_results();
}

void StreamingRanker::report_results()
{
    const size_t n =
        std::min(current_request_.requested_count, scored_results_.size());
    // Sort all scored results to get top requested_count
    std::partial_sort(scored_results_.begin(),
                      scored_results_.begin() + static_cast<std::ptrdiff_t>(n),
                      scored_results_.end(), [](const auto &a, const auto &b) {
                          return a.score > b.score;
                      });

    // Convert top n to FileResult
    accumulated_results_.clear();
    accumulated_results_.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        const auto &result = scored_results_[i];

        // Find the file path from chunk and global index
        size_t global_index = result.index;
        for (size_t chunk_idx = 0;
             chunk_idx < streaming_index_.get_available_chunks(); ++chunk_idx) {
            auto chunk = streaming_index_.get_chunk(chunk_idx);
            if (!chunk)
                break;

            if (global_index < chunk->size()) {
                accumulated_results_.push_back(
                    FileResult{.path = std::string(chunk->at(global_index)),
                               .score = result.score});
                break;
            }
            global_index -= chunk->size();
        }
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
    update.total_available_results =
        scored_results_.size(); // Use flattened results size

    result_updates_.write(std::move(update));
}