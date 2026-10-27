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

#ifndef _LIBCPP___MEMORY_START_LIFETIME_H
#define _LIBCPP___MEMORY_START_LIFETIME_H

#include <__config>
#include <__memory/addressof.h>
#include <__type_traits/is_aggregate.h>
#include <__type_traits/is_const.h>
#include <__type_traits/is_implicit_lifetime.h>
#include <__type_traits/is_volatile.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 26 && __has_builtin(__builtin_start_lifetime)

// [obj.lifetime] (P3726R2): begins the lifetime of the object r refers to, if
// it is not already within its lifetime, without initializing it or starting
// the lifetime of any subobject. If r is a union member, it becomes active.
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr void start_lifetime(_Tp& __r) noexcept {
  static_assert(is_implicit_lifetime_v<_Tp> && is_aggregate_v<_Tp>,
                "std::start_lifetime requires an implicit-lifetime aggregate type");
  static_assert(!is_const_v<_Tp> && !is_volatile_v<_Tp>,
                "std::start_lifetime cannot begin the lifetime of a const or volatile object");
  __builtin_start_lifetime(std::addressof(__r));
}

#endif // _LIBCPP_STD_VER >= 26 && __has_builtin(__builtin_start_lifetime)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_START_LIFETIME_H
