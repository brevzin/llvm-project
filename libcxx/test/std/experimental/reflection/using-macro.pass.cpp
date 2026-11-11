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

// RUN: %{build}
// RUN: %{exec} %t.exe

#include <meta>
#include <debugging>

// using_!(std::chrono::{duration, time_point}) is
//   using std::chrono::duration, std::chrono::time_point
// -- one using-declaration, its declarators grouped by a common prefix,
// nesting (a::{b::{c, d}, e}), at any scope a using-declaration may appear:
// the invocation's ';' ends it.
namespace using_detail {
using std::meta::list_builder;
using std::meta::punctuator;
using std::meta::token_sequence;

// Adds the declarators 'names' designates, each qualified by 'scope'. 'names'
// is a comma-separated list of items, each a name or 'prefix::{ names }'.
consteval void add_declarators(list_builder& out, token_sequence scope,
                               token_sequence names) {
  for (std::size_t pos = 0;;) {
    // The item runs to the next top-level comma, or to the end.
    std::size_t comma = names.balanced_find(punctuator::comma, pos);
    token_sequence item = names.subseq(pos, comma - pos);
    if (item.empty())
      std::consteval_error_str("using_", "expected a name");

    std::size_t open = item.find(punctuator::left_brace);
    if (open == token_sequence::npos) {
      out += scope + item;
    } else {
      if (open == 0 || item[open - 1] != punctuator::colon_colon)
        std::consteval_error_str("using_", "a group must follow '::'");
      std::size_t close = item.balanced_find(punctuator::right_brace, open + 1);
      if (close != item.size() - 1)
        std::consteval_error_str("using_", "nothing may follow a group");
      add_declarators(out, scope + item.subseq(0, open),
                      item.subseq(open + 1, close - open - 1));
    }

    if (comma == token_sequence::npos)
      return;
    pos = comma + 1;
  }
}
}  // namespace using_detail

__macro using_(std::meta::token_sequence names) {
  std::meta::list_builder declarators(^^{ , });
  using_detail::add_declarators(declarators, ^^{}, names);
  return ^^{ using $declarators };
}


#include <cassert>
#include <chrono>

using_!(std::chrono::{duration, time_point});
static_assert(std::is_same_v<duration<int>, std::chrono::duration<int>>);

namespace lib {
int a = 1, b = 2;
namespace inner {
int c = 3, d = 4;
}
int e = 5;
} // namespace lib

using_!(lib::{a, b});
using_!(::lib::{inner::{c, d}, e});  // nested group, global qualifier

struct Base {
  int f() { return 1; }
  int g() { return 2; }
};
struct Derived : private Base {
  using_!(Base::{f, g});  // class scope: the members become public here
};

int block_scope() {
  using_!(lib::inner::{c, d}, lib::e);  // several items, one a group
  return c + d + e;
}

int main(int, char**) {
  assert(a == 1 && b == 2 && c == 3 && d == 4 && e == 5);
  assert(Derived().f() + Derived().g() == 3);
  assert(block_scope() == 12);
  return 0;
}
