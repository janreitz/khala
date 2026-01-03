#include "utility.h"

#include <filesystem>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

std::string serialize_file_info(const fs::path& path) {
    std::ostringstream oss;
    std::error_code ec;
    
    auto status = fs::status(path, ec);
    if (ec) {
        return "Error: " + ec.message();
    }
    
    // Human-readable file size
    auto format_size = [](uintmax_t bytes) -> std::string {
        const char* units[] = {"B", "K", "M", "G", "T", "P"};
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
            s << std::fixed << std::setprecision(1) << size << units[unit_index];
        }
        return s.str();
    };
    
    // File type indicator
    auto type = status.type();
    char type_char = '-';
    if (type == fs::file_type::directory)  type_char = 'd';
    else if (type == fs::file_type::symlink)    type_char = 'l';
    else if (type == fs::file_type::block)      type_char = 'b';
    else if (type == fs::file_type::character)  type_char = 'c';
    else if (type == fs::file_type::fifo)       type_char = 'p';
    else if (type == fs::file_type::socket)     type_char = 's';
    
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
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm* tm = std::localtime(&tt);
    
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", tm);
    
    oss << type_char << perm_str 
        << "  " << std::setw(6) << std::right << size_str
        << "  " << time_buf;
    
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

PackedStrings::PackedStrings()
{
    data_.reserve(1024 * 1024);
    indices_.reserve(16384);
}

void PackedStrings::push(const std::string &str)
{
    indices_.push_back(data_.size());
    data_.insert(data_.end(), str.begin(), str.end());
    data_.push_back('\0');
}

void PackedStrings::merge(PackedStrings &&other)
{
    size_t data_offset = data_.size();

    // Append raw data_
    data_.insert(data_.end(), other.data_.begin(), other.data_.end());

    // Append indices, adjusted by offset
    indices_.reserve(indices_.size() + other.indices_.size());
    for (size_t idx : other.indices_) {
        indices_.push_back(idx + data_offset);
    }
}

std::string_view PackedStrings::at(size_t idx) const
{
    const char *ptr = data_.data() + indices_[idx];
    return std::string_view(ptr);
}

void PackedStrings::shrink_to_fit()
{
    data_.shrink_to_fit();
    indices_.shrink_to_fit();
}

bool PackedStrings::empty() const noexcept { return indices_.empty(); }
size_t PackedStrings::size() const noexcept { return indices_.size(); }

PackedStrings::iterator PackedStrings::begin() const
{
    return iterator(this, 0);
}

PackedStrings::iterator PackedStrings::end() const
{
    return iterator(this, indices_.size());
}

PackedStrings::iterator::iterator(const PackedStrings *container, size_t idx)
    : container_(container), idx_(idx)
{
}

std::string_view PackedStrings::iterator::operator*() const
{
    return container_->at(idx_);
}

std::string_view PackedStrings::iterator::operator[](difference_type n) const
{
    return container_->at(idx_ + n);
}

PackedStrings::iterator &PackedStrings::iterator::operator++()
{
    ++idx_;
    return *this;
}
PackedStrings::iterator PackedStrings::iterator::operator++(int)
{
    iterator tmp = *this;
    ++idx_;
    return tmp;
}
PackedStrings::iterator &PackedStrings::iterator::operator--()
{
    --idx_;
    return *this;
}
PackedStrings::iterator PackedStrings::iterator::operator--(int)
{
    iterator tmp = *this;
    --idx_;
    return tmp;
}

PackedStrings::iterator &PackedStrings::iterator::operator+=(difference_type n)
{
    idx_ += n;
    return *this;
}
PackedStrings::iterator &PackedStrings::iterator::operator-=(difference_type n)
{
    idx_ -= n;
    return *this;
}

PackedStrings::iterator
PackedStrings::iterator::operator+(difference_type n) const
{
    return iterator(container_, idx_ + n);
}
PackedStrings::iterator
PackedStrings::iterator::operator-(difference_type n) const
{
    return iterator(container_, idx_ - n);
}

PackedStrings::iterator::difference_type
PackedStrings::iterator::operator-(const iterator &other) const
{
    return idx_ - other.idx_;
}

bool PackedStrings::iterator::operator==(
    const PackedStrings::iterator &other) const
{
    return idx_ == other.idx_;
}
bool PackedStrings::iterator::operator!=(
    const PackedStrings::iterator &other) const
{
    return idx_ != other.idx_;
}
bool PackedStrings::iterator::operator<(
    const PackedStrings::iterator &other) const
{
    return idx_ < other.idx_;
}
bool PackedStrings::iterator::operator<=(
    const PackedStrings::iterator &other) const
{
    return idx_ <= other.idx_;
}
bool PackedStrings::iterator::operator>(
    const PackedStrings::iterator &other) const
{
    return idx_ > other.idx_;
}
bool PackedStrings::iterator::operator>=(
    const PackedStrings::iterator &other) const
{
    return idx_ >= other.idx_;
}
