#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fuzzy {

// Legacy string-based functions (for compatibility)
float fuzzy_score(std::string_view path, std::string_view query);
float fuzzy_score_2(std::string_view path, std::string_view query);

// PreparedQuery-based functions (optimized)
struct PreparedQuery {
    std::string query_lower;
    uint32_t char_mask;
};
PreparedQuery prepare_query(std::string_view query);

float fuzzy_score(std::string_view path, const PreparedQuery &prepared_query);
float fuzzy_score_2(std::string_view path, const PreparedQuery &prepared_query);
float fuzzy_score_3(std::string_view path, const PreparedQuery &prepared_query);
float fuzzy_score_4(std::string_view path, const PreparedQuery &prepared_query);

// Just find match positions for highlighting (no scoring)
std::vector<size_t> fuzzy_match(std::string_view path, std::string_view query);



}