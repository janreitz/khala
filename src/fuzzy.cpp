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