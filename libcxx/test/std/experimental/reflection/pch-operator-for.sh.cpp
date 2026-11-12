//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23

// The range adaptors' 'operator for's, whose expansion statements wait for
// instantiation, in a precompiled header (as clangd's preamble is): the
// header can be written, and loops in the source use them.

// RUN: mkdir -p %t
// RUN: %{cxx} %{compile_flags} -std=c++2d -freflection -foperator-for \
// RUN:     -x c++-header %s -o %t/header.pch
// RUN: %{cxx} %{compile_flags} %{link_flags} -std=c++2d -freflection \
// RUN:     -foperator-for -include-pch %t/header.pch %s -o %t/main.exe
// RUN: %t/main.exe

#ifndef HEADER
#define HEADER

#include <cassert>
#include <ranges>
#include <vector>

#else

int main(int, char**) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7};
  int sum = 0;
  for (int i : v | std::views::take_while([](int x) { return x < 6; }) |
                   std::views::filter([](int x) { return x % 2; }) |
                   std::views::reverse)
    sum = sum * 10 + i;
  assert(sum == 531);
  return 0;
}

#endif
