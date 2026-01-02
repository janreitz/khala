#include "ranker.h"
#include "fuzzy.h"
#include "lastwriterwinsslot.h"
#include "streamingindex.h"

#include <chrono>
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
                                 LastWriterWinsSlot<ResultUpdate> &results,
                                 std::atomic<RankerMode> &mode,
                                 RankerRequest &request,
                                 std::mutex &request_mutex,
                                 std::atomic_bool &request_changed,
                                 std::atomic_bool &exit_flag)
    : streaming_index_(index), result_updates_(results), ranker_mode_(mode),
      ranker_request_(request), query_mutex_(request_mutex),
      query_changed_(request_changed), should_exit_(exit_flag)
{
}

void StreamingRanker::run()
{
    while (!should_exit_.load(std::memory_order_relaxed)) {
        auto mode = ranker_mode_.load(std::memory_order_acquire);

        if (mode == RankerMode::Inactive) {
            // Sleep when not in file search mode
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (mode == RankerMode::Paused) {
            // Reset state when query is changing
            reset_state();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Check for query or request changes
        bool only_count_increased = false;
        if (query_changed_.exchange(false, std::memory_order_acq_rel)) {
            RankerRequest new_request;
            {
                std::lock_guard lock(query_mutex_);
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

        // Special case: count increased but no new chunks - re-sort existing scored chunks
        if (only_count_increased && processed_chunks_ == streaming_index_.get_available_chunks()) {
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

            // Wait for next query or mode change
            while (ranker_mode_.load(std::memory_order_acquire) ==
                       RankerMode::FileSearch &&
                   !query_changed_.load(std::memory_order_acquire) &&
                   !should_exit_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
}

void StreamingRanker::reset_state()
{
    processed_chunks_ = 0;
    accumulated_results_.clear();
    scored_chunks_.clear();
}

void StreamingRanker::handle_count_increase()
{
    // TODO why do we need to clear here?
    accumulated_results_.clear();

    for (size_t chunk_idx = 0; chunk_idx < scored_chunks_.size(); ++chunk_idx) {
        auto chunk = streaming_index_.get_chunk(chunk_idx);
        if (!chunk)
            break;

        auto &chunk_scored = scored_chunks_[chunk_idx];
        size_t n =
            std::min(current_request_.requested_count, chunk_scored.size());

        // Re-sort with larger n (scores unchanged)
        std::partial_sort(
            chunk_scored.begin(), chunk_scored.begin() + n, chunk_scored.end(),
            [](const auto &a, const auto &b) { return a.score > b.score; });

        // Convert top n to FileResult
        std::vector<FileResult> file_results;
        file_results.reserve(n);
        for (size_t j = 0; j < n; ++j) {
            file_results.push_back(FileResult{
                .path = std::string(chunk->at(chunk_scored[j].index)),
                .score = chunk_scored[j].score});
        }

        // Merge with accumulated results
        accumulated_results_ =
            merge_top_results(accumulated_results_, file_results,
                              current_request_.requested_count);
    }

    send_update();
}

void StreamingRanker::process_chunks()
{
    while (processed_chunks_ < streaming_index_.get_available_chunks()) {
        const auto chunk = streaming_index_.get_chunk(processed_chunks_);
        if (!chunk) {
            break;
        }

        // Get or create scored results for this chunk
        std::vector<RankResult> chunk_scored;

        if (processed_chunks_ < scored_chunks_.size()) {
            // TODO can this ever happen? available_chunks is monotonically increasing
            // Already scored - reuse existing scores
            chunk_scored = scored_chunks_[processed_chunks_];
        } else {
            // New chunk - score it
            std::vector<size_t> indices(chunk->size());
            std::iota(indices.begin(), indices.end(), 0);

            chunk_scored.resize(chunk->size());
            std::transform(std::execution::par_unseq, indices.begin(),
                           indices.end(), chunk_scored.begin(), [&](size_t i) {
                               return RankResult{
                                   i,
                                   fuzzy::fuzzy_score(chunk->at(i),
                                                      current_request_.query)};
                           });

            scored_chunks_.push_back(chunk_scored);
        }

        // Partial sort to get top n from this chunk
        size_t n =
            std::min(current_request_.requested_count, chunk_scored.size());
        std::partial_sort(
            chunk_scored.begin(), chunk_scored.begin() + n, chunk_scored.end(),
            [](const auto &a, const auto &b) { return a.score > b.score; });

        // Convert RankResult to FileResult with actual paths
        std::vector<FileResult> file_results;
        file_results.reserve(n);
        for (size_t j = 0; j < n; ++j) {
            file_results.push_back(FileResult{
                .path = std::string(chunk->at(chunk_scored[j].index)),
                .score = chunk_scored[j].score});
        }

        // Merge with accumulated results
        accumulated_results_ =
            merge_top_results(accumulated_results_, file_results,
                              current_request_.requested_count);

        ++processed_chunks_;

        send_update();
    }
}

void StreamingRanker::send_update(bool is_final)
{
    ResultUpdate update;
    update.results = accumulated_results_;
    update.scan_complete =
        is_final ? true : streaming_index_.is_scan_complete();
    update.total_files = streaming_index_.get_total_files();
    update.processed_chunks = processed_chunks_;

    result_updates_.write(std::move(update));
}