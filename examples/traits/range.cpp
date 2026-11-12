// Copyright 2026 Jump Trading LLC
//
// Range: associated types, and constness as part of Self.

#include "traits.hpp"
#include <cassert>
#include <iterator>
#include <list>
#include <vector>

trait!(Range, {
    typename iterator;
    typename sentinel;
    typename value_type = std::iter_value_t<iterator>;
    requires std::input_or_output_iterator<iterator>;
    requires std::sentinel_for<sentinel, iterator>;

    auto begin() -> iterator;
    auto end() -> sentinel;
    auto empty() -> bool {
        return self.begin() == self.end();
    }
});

using traits::models;

// 1. structural conformance, and constness is part of Self
using V = std::vector<int>;
static_assert(models<V, Range> and models<V const, Range>);
static_assert(std::same_as<Range::iterator<V>, V::iterator>);
static_assert(std::same_as<Range::iterator<V&>, V::iterator>);
static_assert(std::same_as<Range::iterator<V const&>, V::const_iterator>);
static_assert(std::same_as<Range::value_type<V const>, int>);

// 2. explicit conformance for a type we can't add members to: one impl,
//    overloaded on const, gives both Range<T[N]> and Range<T const[N]>
template <class T, std::size_t N>
struct traits::impl<Range, T[N]> {
    static constexpr auto begin(T (&a)[N]) -> T* { return a; }
    static constexpr auto end(T (&a)[N]) -> T* { return a + N; }
    static constexpr auto begin(T const (&a)[N]) -> T const* { return a; }
    static constexpr auto end(T const (&a)[N]) -> T const* { return a + N; }
};
static_assert(std::same_as<Range::iterator<int[3]>, int*>);
static_assert(std::same_as<Range::iterator<int const (&)[3]>, int const*>);

// 3. a caching view: iterable, but not when const
struct caching_view {
    std::vector<int>* base;
    std::vector<int>::iterator cached{};
    auto begin() -> std::vector<int>::iterator { return cached = base->begin(); }
    auto end() -> std::vector<int>::iterator { return base->end(); }
};
static_assert(models<caching_view, Range>);
static_assert(not models<caching_view const, Range>);

// 4. named conformance trumps structure: Grid::begin() means something else
struct Grid {
    std::vector<int> cells;
    auto begin() const -> int { return 0; }  // "the first row index"
};
static_assert(not models<Grid, Range>);  // structurally: int is not an iterator

struct Matrix {
    std::vector<int> cells;
    auto begin() const -> int { return 0; }  // "the first row index"
};
template <>
struct traits::impl<Range, Matrix> {
    static auto begin(Matrix const& m) { return m.cells.begin(); }
    static auto end(Matrix const& m) { return m.cells.end(); }
    static auto empty(Matrix const& m) -> bool { return m.cells.empty(); }  // override default
};
static_assert(models<Matrix, Range>);

// 5. generic code: a proxy that speaks only Range
template <models<Range> R>
constexpr auto sum(R&& r) -> Range::value_type<R> {
    Range::value_type<R> total{};
    for (auto&& e : traits::as<Range>(r)) {  // range-for on the proxy
        total += e;
    }
    return total;
}

constexpr int arr[] = {1, 2, 3, 4};
static_assert(sum(arr) == 10);
static_assert(not Range::empty(arr));

int main() {
    V v = {1, 2, 3};
    assert(sum(v) == 6);
    *Range::begin(v) = 10;
    assert(v[0] == 10);

    caching_view cv{&v};
    assert(sum(cv) == 15);

    Matrix m{{4, 5}};
    assert(sum(m) == 9 and not Range::empty(m));

    std::list<int> l;
    assert(Range::empty(l));
}

// Diagnostics, from traits::check<Range, T>() (or traits::as<Range>(t)):
//
//   Range<const caching_view>: no viable 'begin' (needed to deduce 'iterator')
//   Range<Grid>: requirement 'std::input_or_output_iterator<iterator>' not
//       satisfied with iterator=int sentinel=int value_type=...
