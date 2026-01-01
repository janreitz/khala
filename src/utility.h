#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

template <typename C>
requires std::is_nothrow_invocable_v<std::decay_t<C>>
struct defer {
    explicit defer(C &&callable) : _callable(std::move(callable)) {};
    defer(const defer &other) = delete;
    defer &operator=(const defer &other) = delete;
    defer(defer &&other) = delete;
    defer &operator=(defer &&other) = delete;
    ~defer() { _callable(); };

  private:
    C _callable;
};

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };


struct PackedStrings {

  private:
    std::vector<char> data_;
    std::vector<size_t> indices_;

  public:
    PackedStrings();

    void push(const std::string &str);
    void merge(PackedStrings &&other);
    void shrink_to_fit();

    std::string_view at(size_t idx) const;
    bool empty() const noexcept;
    size_t size() const noexcept;

    class iterator
    {
        const PackedStrings *container_;
        size_t idx_;

      public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;
        using pointer = const std::string_view *;
        using reference = std::string_view;

        iterator(const PackedStrings *container, size_t idx);

        std::string_view operator*() const;

        std::string_view operator[](difference_type n) const;
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