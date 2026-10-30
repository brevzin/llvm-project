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
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_HAS_OPERATOR_FOR

namespace ranges {

// -foperator-for: a range to be iterated backward. A loop over backward(r)
// is a loop over backward_t<R>, whose 'operator for' is found by
// argument-dependent lookup: a view's own (a hidden friend, constrained to
// backward_t of that view) if it has one, or else this one -- its iterators,
// backward, from where ranges::next finds the end (as reverse_view's do).
template <class _Rp>
struct backward_t {
  _Rp&& range;
};

template <class _Rp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr backward_t<_Rp> backward(_Rp&& __r) noexcept {
  return {std::forward<_Rp>(__r)};
}

// (Unconstrained, so that a view's constrained one is more specialized.)
template <class _Rp>
_LIBCPP_HIDE_FROM_ABI void operator for(backward_t<_Rp> __b, __stmt __body) {
  auto&& __r   = std::forward<_Rp>(__b.range);
  auto __first = ranges::begin(__r);
  auto __last  = ranges::next(__first, ranges::end(__r));
  while (__last != __first)
    __body(*--__last);
}

} // namespace ranges

#endif // _LIBCPP_HAS_OPERATOR_FOR

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_BACKWARD_H
