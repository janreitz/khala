#pragma once

#include <algorithm>
#include <atomic>
#include <execution>
#include <mutex>
#include <numeric>
#include <queue>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Forward declarations
class StreamingIndex;
template <typename T> class LastWriterWinsSlot;

// Ranker operation modes
enum class RankerMode {
    Inactive,   // Desktop apps mode - ranker sleeps
    FileSearch, // Active file searching
    Paused      // Query changing - pause work
};

// Basic ranking result with index and score
struct RankResult {
    size_t index;
    float score;

    bool operator>(const RankResult &other) const {
        return score > other.score;
    }
    bool operator<(const RankResult &other) const {
        return score < other.score;
    }
};

// File search result with actual path and score
struct FileResult {
    std::string path;
    float score;

    bool operator>(const FileResult &other) const {
        return score > other.score;
    }
};

// Update message from ranker to UI
struct ResultUpdate {
    std::vector<FileResult> results;
    bool scan_complete = false;
    size_t total_files = 0;
    size_t processed_chunks = 0;
    size_t total_available_results = 0; // Total number of results with score > 0

    ResultUpdate() = default;
    ResultUpdate(std::vector<FileResult> &&results_)
        : results(std::move(results_)) {}
};

// Parallel ranking using std::execution::par_unseq
template <typename ContainerT, typename ScoreFn>
std::vector<RankResult> rank_parallel(const ContainerT &data,
                                      ScoreFn scoring_function, size_t n)
{
    std::vector<size_t> indices(data.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::vector<RankResult> scored(data.size());

    std::transform(std::execution::par_unseq, indices.begin(), indices.end(),
                   scored.begin(), [&](size_t i) {
                       return RankResult{i, scoring_function(data.at(i))};
                   });

    // Partial sort to get top n
    std::partial_sort(
        scored.begin(), scored.begin() + std::min(n, scored.size()),
        scored.end(),
        [](const auto &a, const auto &b) { return a.score > b.score; });

    scored.resize(std::min(n, scored.size()));
    return scored;
}

// Sequential ranking using min-heap
template <typename ContainerT, typename ScoreFn>
std::vector<RankResult> rank(const ContainerT &data, ScoreFn scoring_function,
                             size_t n)
{
    std::vector<RankResult> top_n;
    top_n.reserve(n);

    // Manual heap instead of priority_queue to avoid creating a new vector for return.
    constexpr auto MinHeapCompare = std::greater<>{};
    for (size_t i = 0; i < data.size(); ++i) {
        const float s = scoring_function(data.at(i));
        if (top_n.size() < n) {
            top_n.push_back({i, s});
            std::push_heap(top_n.begin(), top_n.end(), MinHeapCompare);
        // front() -> "min-heap top" -> Min score
        } else if (s > top_n.front().score) {
            std::pop_heap(top_n.begin(), top_n.end(), MinHeapCompare);
            top_n.back() = {i, s};
            std::push_heap(top_n.begin(), top_n.end(), MinHeapCompare);
        }
    }

    std::sort(top_n.begin(), top_n.end(), std::greater<>{}); // descending
    return top_n;
}

// Merge two sorted FileResult vectors, keeping top max_results
std::vector<FileResult> merge_top_results(const std::vector<FileResult> &existing,
                                         const std::vector<FileResult> &new_results,
                                         size_t max_results);

// Ranker request state
struct RankerRequest {
    std::string query;
    size_t requested_count;
};

// Streaming ranker with persistent state for optimized scrolling
class StreamingRanker {
public:
    StreamingRanker(
        StreamingIndex& index,
        LastWriterWinsSlot<ResultUpdate>& results,
        std::atomic<RankerMode>& mode,
        RankerRequest& request,
        std::mutex& request_mutex,
        std::atomic_bool& request_changed,
        std::atomic_bool& exit_flag
    );

    // Main worker loop - run in separate thread
    void run();

private:
    // References to shared state
    StreamingIndex& streaming_index_;
    LastWriterWinsSlot<ResultUpdate>& result_updates_;
    std::atomic<RankerMode>& ranker_mode_;
    RankerRequest& ranker_request_;
    std::mutex& query_mutex_;
    std::atomic_bool& query_changed_;
    std::atomic_bool& should_exit_;

    // Internal state
    size_t processed_chunks_ = 0;
    std::vector<FileResult> accumulated_results_;
    RankerRequest current_request_;
    std::vector<RankResult> scored_results_; // Flattened, only score > 0

    // Helper methods
    void reset_state();
    void handle_count_increase();
    void process_chunks();
    void resort_results();
    void send_update(bool is_final = false);
};