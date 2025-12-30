#include <string_view>
#include <vector>

namespace fuzzy {

float fuzzy_score(std::string_view path, std::string_view query);

// Just find match positions for highlighting (no scoring)
std::vector<size_t> fuzzy_match(std::string_view path, std::string_view query);

}