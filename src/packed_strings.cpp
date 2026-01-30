#include "packed_strings.h"

void PackedStrings::reserve(size_t string_count, size_t expected_avg_string_length)
{
    data_.reserve(string_count * expected_avg_string_length);
    indices_.reserve(string_count);
}

void PackedStrings::prefix(size_t count, char c) {
    // Enter 16 characters padding for SIMD operations searching backwards
    data_.insert(data_.begin(), count, c);
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

PackedStrings::iterator PackedStrings::begin() const { return {this, 0}; }

PackedStrings::iterator PackedStrings::end() const
{
    return {this, indices_.size()};
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
    auto new_idx = static_cast<difference_type>(idx_) + n;
    return container_->at(static_cast<size_t>(new_idx));
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
    idx_ = static_cast<size_t>(static_cast<difference_type>(idx_) + n);
    return *this;
}

PackedStrings::iterator &PackedStrings::iterator::operator-=(difference_type n)
{
    idx_ = static_cast<size_t>(static_cast<difference_type>(idx_) - n);
    return *this;
}

PackedStrings::iterator
PackedStrings::iterator::operator+(difference_type n) const
{
    return {container_,
            static_cast<size_t>(static_cast<difference_type>(idx_) + n)};
}
PackedStrings::iterator
PackedStrings::iterator::operator-(difference_type n) const
{
    return {container_,
            static_cast<size_t>(static_cast<difference_type>(idx_) - n)};
}

PackedStrings::iterator::difference_type
PackedStrings::iterator::operator-(const iterator &other) const
{
    return static_cast<difference_type>(idx_) -
           static_cast<difference_type>(other.idx_);
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