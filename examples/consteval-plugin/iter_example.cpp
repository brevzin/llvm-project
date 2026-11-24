// Copyright 2026 Jump Trading LLC
//
// Consumer TU using iterator_interface via a native consteval plugin — with
// ZERO #include <meta>.

#include "iter_iface.hpp"
#include <cassert>

struct repeated_chars_iterator {
    constexpr repeated_chars_iterator() : first_(nullptr), size_(0), n_(0) {}
    constexpr repeated_chars_iterator(char const* f, std::ptrdiff_t s, std::ptrdiff_t n)
        : first_(f), size_(s), n_(n) {}
    constexpr char operator*() const { return first_[n_ % size_]; }
    constexpr repeated_chars_iterator& operator+=(std::ptrdiff_t i) {
        n_ += i;
        return *this;
    }
    constexpr std::ptrdiff_t operator-(repeated_chars_iterator other) const {
        return n_ - other.n_;
    }

    consteval {
        iface::iterator_interface(^^repeated_chars_iterator,
            {.iterator_concept = ^^std::random_access_iterator_tag, .value_type = ^^char});
    }

private:
    char const* first_;
    std::ptrdiff_t size_;
    std::ptrdiff_t n_;
};

static_assert(std::random_access_iterator<repeated_chars_iterator>);

constexpr bool test_ra() {
    repeated_chars_iterator it("ab", 2, 0);
    auto j = it + 3;
    return *j == 'b' && j - it == 3 && it < j && it[2] == 'a' && it != j;
}
static_assert(test_ra());

struct skip_odd_iterator {
    using base_t = int const*;
    constexpr skip_odd_iterator() : it_(nullptr) {}
    constexpr explicit skip_odd_iterator(int const* it) : it_(it) {}
    constexpr base_t& base_reference() { return it_; }
    constexpr base_t const& base_reference() const { return it_; }
    constexpr skip_odd_iterator& operator++() {
        do {
            ++it_;
        } while (*it_ % 2 != 0);
        return *this;
    }

    consteval {
        iface::iterator_interface(^^skip_odd_iterator,
            {.iterator_concept = ^^std::forward_iterator_tag, .value_type = ^^int});
    }

private:
    int const* it_;
};

static_assert(std::forward_iterator<skip_odd_iterator>);

constexpr bool test_adapt() {
    constexpr int data[] = {2, 3, 5, 4, 7, 6, 0};
    skip_odd_iterator a(data);
    auto b = a;
    ++b;
    if (*b != 4) return false;
    auto c = b++;
    if (*c != 4 || *b != 6) return false;
    return a != b && c != b && a == a;
}
static_assert(test_adapt());

int main() {
    assert(test_ra());
    assert(test_adapt());
}
