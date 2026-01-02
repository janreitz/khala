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

// Sequential ranking using min-heap for efficiency
template <typename ContainerT, typename ScoreFn>
std::vector<RankResult> rank(const ContainerT &data, ScoreFn scoring_function,
                             size_t n)
{
    // Min-heap: smallest score at top, so we can evict it
    std::priority_queue<RankResult, std::vector<RankResult>,
                        std::greater<RankResult>>
        top_n;

    for (size_t i = 0; i < data.size(); ++i) {
        float s = scoring_function(data.at(i));
        if (top_n.size() < n) {
            top_n.push({i, s});
        } else if (s > top_n.top().score) {
            top_n.pop();
            top_n.push({i, s});
        }
    }

    // Extract in descending order
    std::vector<RankResult> result;
    result.reserve(top_n.size());
    while (!top_n.empty()) {
        result.push_back(top_n.top());
        top_n.pop();
    }
    std::reverse(result.begin(), result.end());

    return result;
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
    std::vector<std::vector<RankResult>> scored_chunks_;

    // Helper methods
    void reset_state();
    void handle_count_increase();
    void process_chunks();
    void send_update(bool is_final = false);
};