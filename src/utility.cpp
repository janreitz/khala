#include "utility.h"

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