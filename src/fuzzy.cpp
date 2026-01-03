#include "fuzzy.h"

#include <string>

namespace fuzzy{

float fuzzy_score(std::string_view path, std::string_view query) {
    if (query.empty()) return 1.0f;
    if (path.empty()) return 0.0f;
    
    size_t query_idx = 0;
    size_t last_match = 0;
    float score = 0.0f;
    bool prev_matched = false;
    
    for (size_t i = 0; i < path.size() && query_idx < query.size(); ++i) {
        char pc = std::tolower(static_cast<unsigned char>(path[i]));
        char qc = std::tolower(static_cast<unsigned char>(query[query_idx]));
        
        if (pc == qc) {
            // Base score for match
            score += 1.0f;
            
            // Bonus: consecutive matches
            if (prev_matched) {
                score += 1.5f;
            }
            
            // Bonus: match at word boundary
            if (i == 0 || path[i-1] == '/' || path[i-1] == '_' || path[i-1] == '-' || path[i-1] == '.') {
                score += 2.0f;
            }
            
            // Bonus: match in filename (after last slash)
            // We'll find this lazily
            
            // Penalty: gap between matches
            if (query_idx > 0) {
                size_t gap = i - last_match - 1;
                score -= gap * 0.1f;
            }
            
            last_match = i;
            query_idx++;
            prev_matched = true;
        } else {
            prev_matched = false;
        }
    }
    
    // Did we match the entire query?
    if (query_idx < query.size()) {
        return 0.0f;  // Incomplete match
    }
    
    // Bonus: prefer shorter paths (less noise)
    score -= path.size() * 0.01f;
    
    // Bonus: prefer matches in filename
    size_t last_slash = path.rfind('/');
    if (last_slash != std::string_view::npos && last_match > last_slash) {
        score += 3.0f;
    }
    
    return score;
}

float fuzzy_score_2(std::string_view path, std::string_view query) {
    if (query.empty()) return 1.0f;
    if (path.empty()) return 0.0f;
    
    // Find filename start once
    size_t filename_start = 0;
    if (size_t last_slash = path.rfind('/'); last_slash != std::string_view::npos) {
        filename_start = last_slash + 1;
    }
    
    size_t query_idx = 0;
    size_t last_match = std::string_view::npos;
    float score = 0.0f;
    int consecutive_count = 0;
    bool all_matches_in_filename = true;
    bool is_prefix_of_filename = true;
    
    for (size_t i = 0; i < path.size() && query_idx < query.size(); ++i) {
        char pc = std::tolower(static_cast<unsigned char>(path[i]));
        char qc = std::tolower(static_cast<unsigned char>(query[query_idx]));
        
        if (pc == qc) {
            // Base score for match
            score += 1.0f;
            
            // Track if this is a consecutive match
            bool is_consecutive = (last_match != std::string_view::npos && i == last_match + 1);
            
            if (is_consecutive) {
                consecutive_count++;
                // Quadratic bonus for consecutive runs: 2 + 3 + 4 + ... 
                // This makes long consecutive runs MUCH more valuable
                score += static_cast<float>(consecutive_count + 1);
            } else {
                consecutive_count = 0;
                
                // Penalty for gaps (only between non-consecutive matches)
                if (last_match != std::string_view::npos) {
                    size_t gap = i - last_match - 1;
                    // Steeper gap penalty
                    score -= gap * 0.5f;
                }
            }
            
            // Bonus: match at word boundary (camelCase, snake_case, etc.)
            if (i == 0 || i == filename_start) {
                score += 5.0f;  // Strong bonus for path/filename start
            } else {
                char prev = path[i - 1];
                if (prev == '/' || prev == '_' || prev == '-' || prev == '.' || prev == ' ') {
                    score += 3.0f;
                }
                // CamelCase boundary: lowercase followed by uppercase
                else if (std::islower(static_cast<unsigned char>(prev)) && 
                         std::isupper(static_cast<unsigned char>(path[i]))) {
                    score += 3.0f;
                }
            }
            
            // Track filename matching
            if (i < filename_start) {
                all_matches_in_filename = false;
            }
            if (is_prefix_of_filename && i != filename_start + query_idx) {
                is_prefix_of_filename = false;
            }
            
            last_match = i;
            query_idx++;
        }
    }
    
    // Did we match the entire query?
    if (query_idx < query.size()) {
        return 0.0f;
    }
    
    // Big bonus: all matches are in the filename
    if (all_matches_in_filename) {
        score += 10.0f;
    }
    
    // Huge bonus: query is a prefix of filename (common case: typing filename)
    if (is_prefix_of_filename) {
        score += 15.0f;
    }
    
    // Bonus: exact filename match
    std::string_view filename = path.substr(filename_start);
    if (query.size() == filename.size() && is_prefix_of_filename) {
        score += 20.0f;  // Exact match bonus
    }
    
    // Prefer shorter paths (normalize to avoid huge penalties for deep paths)
    score -= std::min(path.size(), size_t(100)) * 0.02f;
    
    // Prefer shorter filenames when query matches
    score -= std::min(filename.size(), size_t(50)) * 0.05f;
    
    return score;
}

std::vector<size_t> fuzzy_match(std::string_view path, std::string_view query) {
    std::vector<size_t> match_positions;
    
    if (query.empty() || path.empty()) {
        return match_positions;
    }
    
    size_t query_idx = 0;
    
    for (size_t i = 0; i < path.size() && query_idx < query.size(); ++i) {
        char pc = std::tolower(static_cast<unsigned char>(path[i]));
        char qc = std::tolower(static_cast<unsigned char>(query[query_idx]));
        
        if (pc == qc) {
            match_positions.push_back(i);
            query_idx++;
        }
    }
    
    // If we didn't match the entire query, return empty (no highlighting)
    if (query_idx < query.size()) {
        match_positions.clear();
    }
    
    return match_positions;
}

}