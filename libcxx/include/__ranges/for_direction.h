// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_FOR_DIRECTION_H
#define _LIBCPP___RANGES_FOR_DIRECTION_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_HAS_OPERATOR_FOR

// -foperator-for: the direction of a loop, 'for <D> (decl : range)', which a
// range's 'operator for' takes as its template argument (forward by default).
enum class direction : bool { forward, reverse };

_LIBCPP_HIDE_FROM_ABI constexpr direction operator!(direction __d) noexcept {
  return __d == direction::forward ? direction::reverse : direction::forward;
}

#endif // _LIBCPP_HAS_OPERATOR_FOR

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_FOR_DIRECTION_H
