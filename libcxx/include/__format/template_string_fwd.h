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

template <class S>
concept __template_string = requires {
    { S::fmt() } -> same_as<char const*>;
    { S::string(0) } -> same_as<char const*>;
    { S::num_interpolations() } -> integral;
};

template <class S>
concept template_string = __template_string<remove_cvref_t<S>>;

#endif // _LIBCPP_STD_VER >= 26

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS


#endif
