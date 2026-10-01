//===---------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___CSTDDEF_NORETURN_T_H
#define _LIBCPP___CSTDDEF_NORETURN_T_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if defined(__cpp_diverging_expressions)

_LIBCPP_BEGIN_NAMESPACE_STD

// P3549: the bottom type, the type of a diverging expression. It has no
// values; it converts to every type.
using noreturn_t = decltype(throw 0);

_LIBCPP_END_NAMESPACE_STD

#endif // defined(__cpp_diverging_expressions)

#endif // _LIBCPP___CSTDDEF_NORETURN_T_H
