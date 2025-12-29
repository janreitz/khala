#include "utility.h"

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
