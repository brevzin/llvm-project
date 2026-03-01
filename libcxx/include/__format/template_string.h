#ifndef _LIBCPP___FORMAT_TEMPLATE_STRING_H
#define _LIBCPP___FORMAT_TEMPLATE_STRING_H

#include <__format/format_functions.h>
#include <__format/template_string_fwd.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 26

template <template_string _S>
[[nodiscard]] _LIBCPP_ALWAYS_INLINE _LIBCPP_HIDE_FROM_ABI string
format(_S&& __s) {
    auto& [...__parts] = __s;
    return std::format(__s.fmt(), __parts...);
}

template <output_iterator<const char&> _OutIt, template_string _S>
_LIBCPP_ALWAYS_INLINE _LIBCPP_HIDE_FROM_ABI _OutIt
format_to(_OutIt __out_it, _S&& __s) {
    auto& [...__parts] = __s;
    return std::format_to(std::move(__out_it), __s.fmt(), __parts...);
}

template<>
template<template_string _S>
constexpr basic_string<char>::basic_string(_S&& __s)
    : basic_string(std::format((_S&&)__s))
{ }

inline namespace literals {
inline namespace string_literals {

template<template_string _S>
inline _LIBCPP_HIDE_FROM_ABI constexpr string
operator""s(_S&& __s) {
    auto& [...__parts] = __s;
    return std::format(__s.fmt(), __parts...);
}

}
}


#endif // _LIBCPP_STD_VER >= 26

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS


#endif
