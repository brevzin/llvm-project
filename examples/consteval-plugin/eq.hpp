// Copyright 2026 Jump Trading LLC
//
// Consumer header for P4380 [[=Eq]] implemented as a native consteval plugin.
#pragma once

#include <meta_fwd>

template <class T>
concept EqComparable = requires(T const& t) {
    static_cast<bool>(t == t);
};

struct Eq_t {
    consteval auto inject_members(std::meta::info r) const -> std::meta::token_sequence;
};
inline constexpr Eq_t Eq{};
