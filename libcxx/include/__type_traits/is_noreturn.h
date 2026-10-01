//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_NORETURN_H
#define _LIBCPP___TYPE_TRAITS_IS_NORETURN_H

#include <__config>
#include <__type_traits/integral_constant.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if defined(__cpp_diverging_expressions)

_LIBCPP_BEGIN_NAMESPACE_STD

// P3549: the primary type category of std::noreturn_t.
template <class _Tp>
struct _LIBCPP_NO_SPECIALIZATIONS is_noreturn : _BoolConstant<__is_same(__remove_cv(_Tp), decltype(throw 0))> {};

template <class _Tp>
_LIBCPP_NO_SPECIALIZATIONS inline constexpr bool is_noreturn_v = __is_same(__remove_cv(_Tp), decltype(throw 0));

_LIBCPP_END_NAMESPACE_STD

#endif // defined(__cpp_diverging_expressions)

#endif // _LIBCPP___TYPE_TRAITS_IS_NORETURN_H
