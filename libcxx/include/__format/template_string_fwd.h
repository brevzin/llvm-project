#ifndef _LIBCPP___FORMAT_TEMPLATE_STRING_FWD_H
#define _LIBCPP___FORMAT_TEMPLATE_STRING_FWD_H

#include <__concepts/arithmetic.h>
#include <__concepts/same_as.h>
#include <__type_traits/remove_cvref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_HAS_TEMPLATE_STRINGS

template <class T>
concept __decomposable = requires {
    __builtin_structured_binding_size(remove_cvref_t<T>);
};

template <class T>
concept __interpolation =
    same_as<decltype(&T::expression), char const* T::*>
    && same_as<decltype(&T::fmt), char const* T::*>
    && same_as<decltype(&T::index), size_t T::*>
    && same_as<decltype(&T::count), size_t T::*>;

template <class S>
concept __template_string = requires {
    { S::fmt() } -> same_as<char const*>;
    { S::string(0) } -> same_as<char const*>;
    { S::num_interpolations() } -> integral;
    { S::interpolation(0) } -> __interpolation;
};

// The type of a template string literal: the compile-time reflection surface
// (fmt / string / interpolation / num_interpolations, all static consteval)
// plus decomposable bound arguments via exprs().
template <class S>
concept template_string = __template_string<remove_cvref_t<S>> &&
    requires (remove_cvref_t<S> const& s) {
      { s.exprs() } -> __decomposable;
    };

// The vocabulary type pairing a runtime pattern with a template string's
// arguments (see rebind_format in <format>).
template <class _Pattern, template_string _Sp>
class rebound_format;

template <class _Tp>
inline constexpr bool __is_rebound_format_v = false;
template <class _Pattern, class _Sp>
inline constexpr bool __is_rebound_format_v<rebound_format<_Pattern, _Sp>> =
    true;

// What the formatting functions consume: a template string literal
// (compile-time checked pattern) or a rebound_format (runtime pattern).
template <class S>
concept __deferred_format =
    template_string<S> || __is_rebound_format_v<remove_cvref_t<S>>;

#endif // _LIBCPP_HAS_TEMPLATE_STRINGS

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS


#endif
