// Copyright 2026 Jump Trading LLC
//
// Independently compiled native plugin implementation of iterator_interface.
// Build: clang++ ... -fconsteval-plugin -fPIC -shared -O2 iter_iface_plugin.cpp -o libiter_iface.so

// <meta> first: the header shared with consumers includes <meta_fwd>, which
// does nothing after it.
#include <meta>
#include <string_view>
#include <vector>

#include "iter_iface.hpp"

namespace iface {

consteval auto declares_op(std::meta::info cls, std::meta::operators which) -> bool {
    for (auto m : members_of(cls, std::meta::access_context::unchecked())) {
        if (is_operator_function(m) && operator_of(m) == which) {
            return true;
        }
    }
    return false;
}

consteval auto declares_fn(std::meta::info cls, std::string_view name) -> bool {
    for (auto m : members_of(cls, std::meta::access_context::unchecked())) {
        if (has_identifier(m) && identifier_of(m) == name) {
            return true;
        }
    }
    return false;
}

// Declared in iter_iface.hpp.
consteval auto iterator_interface(std::meta::info cls, iter_config cfg) -> void {
    using namespace std::meta;
    auto ts = ^^{
        using iterator_concept = \(cfg.iterator_concept);
        using value_type = \(cfg.value_type);
        using difference_type = \(cfg.difference_type);
    };

    bool adaptor = declares_fn(cls, "base_reference");
    bool plus_eq = declares_op(cls, op_plus_equals);
    bool minus = declares_op(cls, op_minus);
    bool pre_inc = declares_op(cls, op_plus_plus);
    bool pre_dec = declares_op(cls, op_minus_minus);
    bool eq = declares_op(cls, op_equals_equals);
    bool deref = declares_op(cls, op_star);

    if (adaptor) {
        info base_t{};
        for (info m : members_of(cls, access_context::unchecked())) {
            if (has_identifier(m) && identifier_of(m) == "base_reference") {
                base_t = remove_reference(return_type_of(m));
                break;
            }
        }
        bool base_deref = extract<bool>(substitute(^^base_can_deref, {base_t}));
        bool base_pre_inc = extract<bool>(substitute(^^base_can_pre_inc, {base_t}));
        bool base_eq = extract<bool>(substitute(^^base_can_eq, {base_t}));

        if (base_deref && !deref) {
            ts += ^^{ constexpr decltype(auto) operator*() const { return *base_reference(); } };
        }
        if (base_pre_inc && !pre_inc) {
            ts += ^^{ constexpr auto operator++() -> \(cls)& { ++base_reference(); return *this; } };
        }
        if (base_pre_inc) {
            ts += ^^{ constexpr auto operator++(int) -> \(cls) { auto tmp = *this; ++*this; return tmp; } };
        }
        if (base_eq && !eq) {
            ts += ^^{
                friend constexpr bool operator==(\(cls) const& a, \(cls) const& b) {
                    return a.base_reference() == b.base_reference();
                }
            };
        }
        queue_injection(ts);
        return;
    }

    if (plus_eq) {
        ts += ^^{
            constexpr auto operator++() -> \(cls)& { *this += 1; return *this; }
            constexpr auto operator++(int) -> \(cls) { auto tmp = *this; ++*this; return tmp; }
            constexpr auto operator--() -> \(cls)& { *this += -1; return *this; }
            constexpr auto operator--(int) -> \(cls) { auto tmp = *this; --*this; return tmp; }
            constexpr auto operator-=(std::ptrdiff_t n) -> \(cls)& { *this += -n; return *this; }
            constexpr decltype(auto) operator[](std::ptrdiff_t n) const { auto tmp = *this; tmp += n; return *tmp; }
            friend constexpr auto operator+(\(cls) it, std::ptrdiff_t n) -> \(cls) { it += n; return it; }
            friend constexpr auto operator+(std::ptrdiff_t n, \(cls) it) -> \(cls) { it += n; return it; }
            friend constexpr auto operator-(\(cls) it, std::ptrdiff_t n) -> \(cls) { it += -n; return it; }
        };
    }
    else {
        if (pre_inc) {
            ts += ^^{ constexpr auto operator++(int) -> \(cls) { auto tmp = *this; ++*this; return tmp; } };
        }
        if (pre_dec) {
            ts += ^^{ constexpr auto operator--(int) -> \(cls) { auto tmp = *this; --*this; return tmp; } };
        }
    }
    if (minus) {
        if (!eq) {
            ts += ^^{
                friend constexpr bool operator==(\(cls) const& a, \(cls) const& b) { return (a - b) == 0; }
            };
        }
        ts += ^^{
            friend constexpr auto operator<=>(\(cls) const& a, \(cls) const& b) { return (a - b) <=> 0; }
        };
    }
    queue_injection(ts);
}

}  // namespace iface
