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

#ifndef _LIBCPP___RANGES_BACKWARD_H
#define _LIBCPP___RANGES_BACKWARD_H

#include <__config>
#include <__iterator/next.h>
#include <__ranges/access.h>
#include <__type_traits/remove_reference.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_HAS_OPERATOR_FOR

namespace ranges {

// -foperator-for: a range to be iterated backward. A loop over backward(r) is
// a loop over backward_t<R>, whose 'operator for' is found by
// argument-dependent lookup: a view's own -- a hidden friend,
//
//   friend void operator for(backward_t<V> __b, __stmt __body);
//
// -- if it has one, or else this one: the range's iterators, backward, from
// where ranges::next finds the end (as reverse_view's do).
template <class _Rp>
struct backward_t {
  _Rp& range;
};

template <class _Rp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr backward_t<remove_reference_t<_Rp>> backward(_Rp&& __r) noexcept {
  return {__r};
}

template <class _Rp>
_LIBCPP_HIDE_FROM_ABI void operator for(backward_t<_Rp> __b, __stmt __body) {
  auto __first = ranges::begin(__b.range);
  auto __last  = ranges::next(__first, ranges::end(__b.range));
  while (__last != __first)
    __body(*--__last);
}

} // namespace ranges

#endif // _LIBCPP_HAS_OPERATOR_FOR

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_BACKWARD_H
