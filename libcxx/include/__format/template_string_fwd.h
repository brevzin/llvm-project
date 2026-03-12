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

#if _LIBCPP_STD_VER >= 26

template <class _CharT>
struct __runtime_format_string;

template <class T>
concept __suitable_fmt = same_as<decay_t<T>, char const*>
                      || same_as<decay_t<T>, __runtime_format_string<char>>;

template <class T>
concept __decomposable = requires {
    __builtin_structured_binding_size(remove_cvref_t<T>);
};

template <class S>
concept weak_template_string = requires (S& s) {
    { s.fmt() } -> __suitable_fmt;
    { s.exprs() } -> __decomposable;
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

template <class S>
concept template_string = weak_template_string<S>
                       && __template_string<remove_cvref_t<S>>;

#endif // _LIBCPP_STD_VER >= 26

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS


#endif
