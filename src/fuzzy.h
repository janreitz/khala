#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fuzzy {

// Fuzzy scoring functions
// The functions expect lowercase queries and do not perform case conversion internally.
float fuzzy_score(std::string_view path, std::string_view query);
float fuzzy_score_2(std::string_view path, std::string_view query);
float fuzzy_score_3(std::string_view path, std::string_view query);
float fuzzy_score_4(std::string_view path, std::string_view query);
float fuzzy_score_5(std::string_view path, std::string_view query);

// Find match positions for highlighting (no scoring)
// Query parameter must be pre-lowercased
std::vector<size_t> fuzzy_match(std::string_view path, std::string_view query);
std::vector<size_t> fuzzy_match_optimal(std::string_view path, std::string_view query);



}