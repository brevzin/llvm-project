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


namespace lib {
int a, b;
}

using_!(lib{a, b});
// expected-error@-1 {{reported an error}}
// expected-note@*:* {{a group must follow '::'}}
using_!(lib::{a, b}::c);
// expected-error@-1 {{reported an error}}
// expected-note@*:* {{nothing may follow a group}}
using_!(lib::{a}::{b});
// expected-error@-1 {{reported an error}}
// expected-note@*:* {{nothing may follow a group}}
using_!(lib::a, );
// expected-error@-1 {{reported an error}}
// expected-note@*:* {{expected a name}}
using_!(lib::{});
// expected-error@-1 {{reported an error}}
// expected-note@*:* {{expected a name}}
// expected-note@*:* 0+ {{}}
