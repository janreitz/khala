#include "ranker.h"

std::vector<FileResult> merge_top_results(const std::vector<FileResult> &existing,
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