// Copyright 2026 Jump Trading LLC
//
// Predicate<Arg>: a trait template, operator(), and coercion that defeats Evil.

#include "traits.hpp"
#include <cassert>
#include <vector>

trait!(Predicate<class Arg>, { auto operator()(Arg) const->bool; });

// Evil is convertible to bool, but && on it doesn't mean what you think
struct Evil {
    bool b;
    constexpr explicit(false) operator bool() const { return b; }
    friend constexpr auto operator&&(Evil, bool) -> Evil { return {false}; }
};
constexpr auto is_even = [](int i) { return Evil{i % 2 == 0}; };
constexpr auto is_small = [](int i) { return i < 10; };

using traits::models;
static_assert(models<decltype(is_even), Predicate<int>>);
static_assert(models<bool (*)(int), Predicate<int>>);
static_assert(not models<int, Predicate<int>>);
static_assert(not models<decltype(is_even), Predicate<std::vector<int>>>);

template <models<Predicate<int>> P, models<Predicate<int>> Q>
constexpr auto count_both(std::vector<int> const& v, P p_, Q q_) -> int {
    auto p = traits::as<Predicate<int>>(p_);
    auto q = traits::as<Predicate<int>>(q_);
    int n = 0;
    for (int i : v) {
        if (p(i) && q(i)) {  // p(i) is a bool: the trait says so, whatever P says
            ++n;
        }
    }
    return n;
}
static_assert(count_both({1, 2, 4, 11, 12}, is_even, is_small) == 2);

// the CPO
static_assert(Predicate<int>::call(is_even, 4));
static_assert(std::same_as<decltype(Predicate<int>::call(is_even, 4)), bool>);

// explicit conformance for a type we don't own, via static operator()
struct Threshold {
    int t;
};
template <>
struct traits::impl<Predicate<int>, Threshold> {
    static constexpr auto operator()(Threshold const& th, int i) -> bool { return i > th.t; }
};
static_assert(count_both({1, 2, 4, 11, 12}, is_even, Threshold{3}) == 2);

// dyn of a one-function trait is a function_ref
static bool odd(int i) {
    return i % 2;
}
int main() {
    Threshold th{5};
    std::vector<Predicate<int>::dyn> preds = {is_even, is_small, th};
    bool (*fp)(int) = odd;
    preds.push_back(fp);
    int hits = 0;
    for (auto const& p : preds) {
        hits += p(6);
    }
    assert(hits == 3);
}
