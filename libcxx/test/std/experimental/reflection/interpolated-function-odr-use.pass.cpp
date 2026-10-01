//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// An interpolated reflection of a function (here a function template
// specialization formed by substitute, never named in the source) is used
// where the expansion lands: the specialization is instantiated and emitted,
// so the program links and the call runs.

#include <meta>

template <class T>
T twice(T x) {
  return x * 2;
}

template <class T>
__macro call_twice(T x) {
  static constexpr auto fn = substitute(^^twice, {^^T});
  return ^^{ \(fn)(\(x)) };
}

int main(int, char**) {
  int i = 21;
  long l = 5;
  return (call_twice!(i) == 42 && call_twice!(l) == 10) ? 0 : 1;
}
