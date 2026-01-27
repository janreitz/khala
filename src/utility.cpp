#include "utility.h"
#include "logger.h"

#include <emmintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <fstream>

namespace fs = std::filesystem;

std::string to_string(const ui::KeyboardEvent &hotkey)
{
    std::string result;

    // Add modifiers
    if (ui::has_modifier(hotkey.modifiers, ui::KeyModifier::Ctrl)) {
        result += "Ctrl+";
    }
    if (ui::has_modifier(hotkey.modifiers, ui::KeyModifier::Alt)) {
        result += "Alt+";
    }
    if (ui::has_modifier(hotkey.modifiers, ui::KeyModifier::Shift)) {
        result += "Shift+";
    }
    if (ui::has_modifier(hotkey.modifiers, ui::KeyModifier::Super)) {
        result += "Super+";
    }

    // Add key
    switch (hotkey.key) {
    case ui::KeyCode::Escape:
        result += "Escape";
        break;
    case ui::KeyCode::Return:
        result += "Return";
        break;
    case ui::KeyCode::BackSpace:
        result += "BackSpace";
        break;
    case ui::KeyCode::Delete:
        result += "Delete";
        break;
    case ui::KeyCode::Tab:
        result += "Tab";
        break;
    case ui::KeyCode::Space:
        result += "Space";
        break;
    case ui::KeyCode::Up:
        result += "Up";
        break;
    case ui::KeyCode::Down:
        result += "Down";
        break;
    case ui::KeyCode::Left:
        result += "Left";
        break;
    case ui::KeyCode::Right:
        result += "Right";
        break;
    case ui::KeyCode::Home:
        result += "Home";
        break;
    case ui::KeyCode::End:
        result += "End";
        break;
    // Letters A-Z
    case ui::KeyCode::A:
        result += "A";
        break;
    case ui::KeyCode::B:
        result += "B";
        break;
    case ui::KeyCode::C:
        result += "C";
        break;
    case ui::KeyCode::D:
        result += "D";
        break;
    case ui::KeyCode::E:
        result += "E";
        break;
    case ui::KeyCode::F:
        result += "F";
        break;
    case ui::KeyCode::G:
        result += "G";
        break;
    case ui::KeyCode::H:
        result += "H";
        break;
    case ui::KeyCode::I:
        result += "I";
        break;
    case ui::KeyCode::J:
        result += "J";
        break;
    case ui::KeyCode::K:
        result += "K";
        break;
    case ui::KeyCode::L:
        result += "L";
        break;
    case ui::KeyCode::M:
        result += "M";
        break;
    case ui::KeyCode::N:
        result += "N";
        break;
    case ui::KeyCode::O:
        result += "O";
        break;
    case ui::KeyCode::P:
        result += "P";
        break;
    case ui::KeyCode::Q:
        result += "Q";
        break;
    case ui::KeyCode::R:
        result += "R";
        break;
    case ui::KeyCode::S:
        result += "S";
        break;
    case ui::KeyCode::T:
        result += "T";
        break;
    case ui::KeyCode::U:
        result += "U";
        break;
    case ui::KeyCode::V:
        result += "V";
        break;
    case ui::KeyCode::W:
        result += "W";
        break;
    case ui::KeyCode::X:
        result += "X";
        break;
    case ui::KeyCode::Y:
        result += "Y";
        break;
    case ui::KeyCode::Z:
        result += "Z";
        break;
    // Numbers 0-9
    case ui::KeyCode::Num0:
        result += "0";
        break;
    case ui::KeyCode::Num1:
        result += "1";
        break;
    case ui::KeyCode::Num2:
        result += "2";
        break;
    case ui::KeyCode::Num3:
        result += "3";
        break;
    case ui::KeyCode::Num4:
        result += "4";
        break;
    case ui::KeyCode::Num5:
        result += "5";
        break;
    case ui::KeyCode::Num6:
        result += "6";
        break;
    case ui::KeyCode::Num7:
        result += "7";
        break;
    case ui::KeyCode::Num8:
        result += "8";
        break;
    case ui::KeyCode::Num9:
        result += "9";
        break;
    // Function keys
    case ui::KeyCode::F1:
        result += "F1";
        break;
    case ui::KeyCode::F2:
        result += "F2";
        break;
    case ui::KeyCode::F3:
        result += "F3";
        break;
    case ui::KeyCode::F4:
        result += "F4";
        break;
    case ui::KeyCode::F5:
        result += "F5";
        break;
    case ui::KeyCode::F6:
        result += "F6";
        break;
    case ui::KeyCode::F7:
        result += "F7";
        break;
    case ui::KeyCode::F8:
        result += "F8";
        break;
    case ui::KeyCode::F9:
        result += "F9";
        break;
    case ui::KeyCode::F10:
        result += "F10";
        break;
    case ui::KeyCode::F11:
        result += "F11";
        break;
    case ui::KeyCode::F12:
        result += "F12";
        break;
    default:
        result += "Unknown";
        break;
    }

    return result;
}

std::string serialize_file_info(const fs::path &path)
{
    std::ostringstream oss;
    std::error_code ec;

    auto status = fs::status(path, ec);
    if (ec) {
        return "Error: " + ec.message();
    }

    // Human-readable file size
    auto format_size = [](uintmax_t bytes) -> std::string {
        const char *units[] = {"B", "K", "M", "G", "T", "P"};
        int unit_index = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit_index < 5) {
            size /= 1024.0;
            ++unit_index;
        }

        std::ostringstream s;
        if (unit_index == 0) {
            s << bytes << units[unit_index];
        } else {
            s << std::fixed << std::setprecision(1) << size
              << units[unit_index];
        }
        return s.str();
    };

    // File type indicator
    auto type = status.type();
    char type_char = '-';
    if (type == fs::file_type::directory)
        type_char = 'd';
    else if (type == fs::file_type::symlink)
        type_char = 'l';
    else if (type == fs::file_type::block)
        type_char = 'b';
    else if (type == fs::file_type::character)
        type_char = 'c';
    else if (type == fs::file_type::fifo)
        type_char = 'p';
    else if (type == fs::file_type::socket)
        type_char = 's';

    // Permissions string (rwxrwxrwx)
    auto perms = status.permissions();
    auto perm_char = [](fs::perms p, fs::perms check, char c) {
        return (p & check) != fs::perms::none ? c : '-';
    };

    std::string perm_str;
    perm_str += perm_char(perms, fs::perms::owner_read, 'r');
    perm_str += perm_char(perms, fs::perms::owner_write, 'w');
    perm_str += perm_char(perms, fs::perms::owner_exec, 'x');
    perm_str += perm_char(perms, fs::perms::group_read, 'r');
    perm_str += perm_char(perms, fs::perms::group_write, 'w');
    perm_str += perm_char(perms, fs::perms::group_exec, 'x');
    perm_str += perm_char(perms, fs::perms::others_read, 'r');
    perm_str += perm_char(perms, fs::perms::others_write, 'w');
    perm_str += perm_char(perms, fs::perms::others_exec, 'x');

    // File size
    std::string size_str = "   -";
    if (type == fs::file_type::regular) {
        size_str = format_size(fs::file_size(path, ec));
    }

    // Last modified time
    auto ftime = fs::last_write_time(path, ec);
    auto sctp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm *tm = std::localtime(&tt);

    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", tm);

    oss << type_char << perm_str << "  " << std::setw(6) << std::right
        << size_str << "  " << time_buf;

    return oss.str();
};

std::string to_lower(std::string_view str)
{
    std::string lower_case_string;
    lower_case_string.reserve(str.size());

    for (char c : str) {
        unsigned char lc = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(c)));
        lower_case_string.push_back(static_cast<char>(lc));
    }

    return lower_case_string;
}

std::string read_file(const fs::path &path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    if (file.bad()) {
        throw std::runtime_error("Error reading file: " + path.string());
    }

    return buffer.str();
}


int find_last_or(std::string_view str, char c, int _default)
{
    const auto *data = str.data();
    const auto len = str.length();
    const char *p = data + len;
    while (p > data && *--p != c) {
    }
    if (*p == c) {
        return static_cast<int>(p - data);
    } else {
        return _default;
    }
}

int count_leading_zeros(unsigned int x)
{
#ifdef _MSC_VER
    unsigned long index;
    unsigned char success = _BitScanReverse(&index, x);
    // _BitScanReverse fails for x = 0
    // If success == 0, return 32 
    // If success == 1, we want to return 31 - index
    return 32 - (success * (index + 1));
#else
    return __builtin_clz(x);
#endif
}

// This requires up to sizeof(__m128i) before str.data();
int simd_find_last_or(std::string_view str, char c, int _default)
{
    // Set all lanes equal to '/'
    const auto compare_against = _mm_set1_epi8(c);
    int offset = static_cast<int>(str.length());
    const auto data = str.data();
    while (offset >= 0) {
        offset -= static_cast<int>(sizeof(__m128i));
        const char *p = data + offset;
        const auto compare_this =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(p));
        const __m128i cmp_result =
            _mm_cmpeq_epi8(compare_this, compare_against);
        // Sets bits for each matched character (only uses the first 16 bits)
        const auto match_mask =
            static_cast<unsigned int>(_mm_movemask_epi8(cmp_result));
        if (match_mask != 0) {
            const int last_match_index_in_chunk =
                (static_cast<int>(sizeof(int)) * 8 - 1) -
                count_leading_zeros(match_mask);
            const int last_match_index = offset + last_match_index_in_chunk;
            return last_match_index >= 0 ? last_match_index : _default;
        }
    }
    return _default;
}

// Returns index of first occurrence of c at or after start, or -1 if not found
int simd_find_first_or(const char *data, size_t len, char c, size_t start,
                       int _default)
{
    size_t offset = start;
#if defined(__SSE2__)
    const __m128i compare_against = _mm_set1_epi8(c);
    while (offset + 16 <= len) {
        const __m128i chunk =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + offset));
        const __m128i cmp_result = _mm_cmpeq_epi8(chunk, compare_against);
        const unsigned int match_mask =
            static_cast<unsigned int>(_mm_movemask_epi8(cmp_result));

        if (match_mask != 0) {
            return static_cast<int>(offset) + __builtin_ctz(match_mask);
        }
        offset += 16;
    }
#endif
    // Scalar tail
    while (offset < len) {
        if (data[offset] == c) {
            return static_cast<int>(offset);
        }
        offset++;
    }

    return _default;
}

void simd_to_lower(const char *src, size_t len, char *out_buffer)
{
    size_t i = 0;

#if defined(__SSE2__)
    const __m128i upper_a = _mm_set1_epi8('A' - 1);
    const __m128i upper_z = _mm_set1_epi8('Z' + 1);
    const __m128i lower_bit = _mm_set1_epi8(0x20);
    for (; i + 16 <= len; i += 16) {
        __m128i chunk =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + i));

        __m128i ge_a = _mm_cmpgt_epi8(chunk, upper_a);
        __m128i le_z = _mm_cmpgt_epi8(upper_z, chunk);
        __m128i is_upper = _mm_and_si128(ge_a, le_z);

        __m128i to_add = _mm_and_si128(is_upper, lower_bit);
        __m128i result = _mm_add_epi8(chunk, to_add);

        _mm_storeu_si128(reinterpret_cast<__m128i *>(out_buffer + i), result);
    }
#endif

    // Scalar fallback for remainder
    for (; i < len; ++i) {
        const char c = static_cast<char>(src[i]);
        out_buffer[i] =
            (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 0x20) : c;
    }
}

// Returns positions of all matches (up to max_results)
size_t find_all(const char *data, size_t len, char target, size_t *positions,
                size_t max_results)
{
    size_t pos_idx = 0;
    for (size_t data_idx = 0; data_idx < len && pos_idx < max_results;
         ++data_idx) {
        if (data[data_idx] == target) {
            positions[pos_idx++] = data_idx;
        }
    }
    return pos_idx;
}

// Returns positions of all matches (up to max_results)
size_t simd_find_all(const char *data, size_t len, char target,
                     size_t *positions, size_t max_results)
{
    size_t pos_idx = 0;
    size_t data_idx = 0;

#if defined(__SSE2__)
    const __m128i target_vec = _mm_set1_epi8(target);

    // Process 16 bytes at a time
    while (data_idx + 16 <= len && pos_idx < max_results) {
        const __m128i chunk =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + data_idx));
        const __m128i cmp = _mm_cmpeq_epi8(chunk, target_vec);
        int mask = _mm_movemask_epi8(cmp);

        while (mask != 0 && pos_idx < max_results) {
            const auto bit_pos = static_cast<size_t>(__builtin_ctz(
                static_cast<unsigned int>(mask))); // Position of lowest set bit
            positions[pos_idx++] = data_idx + bit_pos;
            mask &= mask - 1; // Clear lowest set bit
        }

        data_idx += 16;
    }
#endif
    // Scalar tail
    while (data_idx < len && pos_idx < max_results) {
        if (data[data_idx] == target) {
            positions[pos_idx++] = data_idx;
        }
        data_idx++;
    }

    return pos_idx;
}

void load_history(PackedStrings& history) {
    const auto path = platform::get_history_path();
    if (!fs::exists(path)) {
        LOG_INFO("No history file at %s", path.c_str());
        return;
    }

    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            history.push(line);
        }
    }
    LOG_INFO("Loaded %zu history entries from %s", history.size(), path.c_str());
}

void save_history(const PackedStrings& history) {
    const auto path = platform::get_history_path();
    std::error_code err;
    fs::create_directories(path.parent_path(), err);
    if (err) {
        LOG_ERROR("Failed to create history directory: %s", err.message().c_str());
        return;
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        LOG_ERROR("Failed to open history file for writing: %s", path.c_str());
        return;
    }
    constexpr size_t MAX_HISTORY = 1000;
    size_t start = history.size() > MAX_HISTORY ? history.size() - MAX_HISTORY : 0;
    for (size_t i = start; i < history.size(); i++) {
        file << history.at(i) << '\n';
    }
    LOG_INFO("Saved %zu history entries to %s", history.size() - start, path.c_str());
}

std::optional<std::filesystem::path> get_dir(std::string_view path)
{
    fs::path dir(path);
    return fs::exists(dir) && fs::is_directory(dir)
               ? std::optional<fs::path>(dir)
               : std::optional<fs::path>(std::nullopt);
}

fs::path get_history_path() { return platform::get_data_dir() / "history.txt"; }