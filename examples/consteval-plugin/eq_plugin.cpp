// Copyright 2026 Jump Trading LLC
//
// Native consteval plugin implementation of P4380 [[=Eq]].

// <meta> first: the header shared with consumers includes <meta_fwd>, which
// does nothing after it.
#include <algorithm>
#include <meta>

#include "eq.hpp"

// Declared in eq.hpp.
consteval auto Eq_t::inject_members(std::meta::info r) const -> std::meta::token_sequence {
    auto subobjects = subobjects_of(r, std::meta::access_context::unchecked());
    if (not std::ranges::all_of(subobjects, [](std::meta::info o) {
            return extract<bool>(substitute(^^EqComparable, {type_of(o)}));
        })) {
        return ^^{
            friend constexpr bool operator==(\(r) const&, \(r) const&) = delete;
            friend constexpr bool operator!=(\(r) const&, \(r) const&) = delete;
        };
    }

    std::meta::list_builder cmp(^^{ && });
    for (std::meta::info s : subobjects) {
        cmp += ^^{ (__lhs.\(s) == __rhs.\(s)) };
    }
    if (subobjects.empty()) {
        cmp += ^^{ true };
    }

    return ^^{
        friend constexpr bool operator==(\(r) const& __lhs [[maybe_unused]],
                                         \(r) const& __rhs [[maybe_unused]]) {
            return \(cmp);
        }
        friend constexpr bool operator!=(\(r) const& __lhs, \(r) const& __rhs) {
            return !(__lhs == __rhs);
        }
    };
}
