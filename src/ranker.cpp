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
    scored_results_.clear();
}

void StreamingRanker::handle_count_increase()
{
    // Just rebuild accumulated results with new count
    resort_results();
}

void StreamingRanker::process_chunks()
{
    // Calculate global offset for new chunks
    size_t global_offset = 0;
    for (size_t i = 0; i < processed_chunks_; ++i) {
        auto chunk = streaming_index_.get_chunk(i);
        if (chunk) {
            global_offset += chunk->size();
        }
    }

    while (processed_chunks_ < streaming_index_.get_available_chunks()) {
        const auto chunk = streaming_index_.get_chunk(processed_chunks_);
        if (!chunk) {
            break;
        }

        // Score new chunk and add results with score > 0 to flattened results
        std::vector<size_t> indices(chunk->size());
        std::iota(indices.begin(), indices.end(), 0);

        std::vector<RankResult> chunk_scored(chunk->size());
        std::transform(std::execution::par_unseq, indices.begin(),
                       indices.end(), chunk_scored.begin(), [&](size_t i) {
                           return RankResult{
                               global_offset + i, // Use global index
                               fuzzy::fuzzy_score_4(chunk->at(i),
                                                  current_request_.query)};
                       });

        // Add only results with score > 0 to flattened results
        for (const auto& result : chunk_scored) {
            if (result.score > 0.0f) {
                scored_results_.push_back(result);
            }
        }

        // Update global offset for next chunk
        global_offset += chunk->size();
        ++processed_chunks_;

        // Incrementally rebuild accumulated results
        resort_results();
    }
}

void StreamingRanker::resort_results()
{
    const size_t n = std::min(current_request_.requested_count, scored_results_.size());
    // Sort all scored results to get top requested_count
    std::partial_sort(scored_results_.begin(), 
                      scored_results_.begin() + n,
                      scored_results_.end(),
                      [](const auto &a, const auto &b) { return a.score > b.score; });

    // Convert top n to FileResult  
    accumulated_results_.clear();
    accumulated_results_.reserve(n);
    
    for (size_t i = 0; i < n; ++i) {
        const auto& result = scored_results_[i];
        
        // Find the file path from chunk and global index
        size_t global_index = result.index;
        for (size_t chunk_idx = 0; chunk_idx < streaming_index_.get_available_chunks(); ++chunk_idx) {
            auto chunk = streaming_index_.get_chunk(chunk_idx);
            if (!chunk) break;
            
            if (global_index < chunk->size()) {
                accumulated_results_.push_back(FileResult{
                    .path = std::string(chunk->at(global_index)),
                    .score = result.score
                });
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
    update.total_available_results = scored_results_.size(); // Use flattened results size

    result_updates_.write(std::move(update));
}