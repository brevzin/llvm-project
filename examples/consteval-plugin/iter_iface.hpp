// Copyright 2026 Jump Trading LLC
//
// Consumer header for the independently compiled iterator_interface plugin.
// Consumers need no <meta>, <vector> or <ranges>. The plugin includes this
// header too (after <meta>), so there is one declaration of everything the
// two share.
#pragma once

#include <cstddef>
#include <iterator>
#include <meta_fwd>

namespace iface {

using std::meta::info;

// Probe templates for the adaptor path (substituted via reflection by the plugin).
template <class B> constexpr bool base_can_deref = requires(B const& b) { *b; };
template <class B> constexpr bool base_can_pre_inc = requires(B& b) { ++b; };
template <class B>
constexpr bool base_can_eq = requires(B const& a, B const& b) {
    { a == b } -> std::convertible_to<bool>;
};

struct iter_config {
    info iterator_concept;
    info value_type;
    info difference_type = ^^std::ptrdiff_t;
};

// Implemented natively in libiter_iface.so (-fconsteval-plugin).
consteval auto iterator_interface(info cls, iter_config cfg) -> void;

}  // namespace iface
