#pragma once

#include "str.h"

#include <string>
#include <vector>


struct PackedStrings {

  private:
    std::vector<char> data_;
    std::vector<size_t> indices_;

  public:
    PackedStrings() = default;

    void reserve(size_t string_count, size_t expected_avg_string_length);
    void push(const std::string &str);
    void merge(PackedStrings &&other);
    void shrink_to_fit();

    void prefix(size_t count, char c);

    StrView at(size_t idx) const;
    bool empty() const noexcept;
    size_t size() const noexcept;

    class iterator
    {
        const PackedStrings *container_;
        size_t idx_;

      public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = StrView;
        using difference_type = std::ptrdiff_t;
        using pointer = const StrView *;
        using reference = StrView;

        iterator(const PackedStrings *container, size_t idx);

        StrView operator*() const;

        StrView operator[](difference_type n) const;
        iterator &operator++();
        iterator operator++(int);
        iterator &operator--();
        iterator operator--(int);
        iterator &operator+=(difference_type n);
        iterator &operator-=(difference_type n);
        iterator operator+(difference_type n) const;
        iterator operator-(difference_type n) const;
        difference_type operator-(const iterator &other) const;
        bool operator==(const iterator &other) const;
        bool operator!=(const iterator &other) const;
        bool operator<(const iterator &other) const;
        bool operator<=(const iterator &other) const;
        bool operator>(const iterator &other) const;
        bool operator>=(const iterator &other) const;
    };

    iterator begin() const;
    iterator end() const;
};