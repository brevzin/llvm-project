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
#include <algorithm>
#include <debugging>
#include <functional>
#include <ranges>
#include <vector>

// using_!(std::chrono::{duration, time_point}) is
//   using std::chrono::duration; using std::chrono::time_point
// -- grouped using-declarations, nesting (a::{b::{c, d}, e}), at any scope a
// using-declaration may appear: the invocation's ';' ends the last one.
namespace using_detail {
using std::meta::punctuator;
using std::meta::token_kind;
using std::meta::token_sequence;

consteval bool is(token_sequence t, punctuator p) {
  return token_kind_of(t) == token_kind::punctuator && punctuator_of(t) == p;
}

// A range of tokens back into one sequence.
consteval token_sequence join(auto&& r) {
  return std::ranges::fold_left(r, ^^{}, std::plus<>());
}

// Split at top-level commas.
consteval auto split_commas(auto&& r) -> std::vector<token_sequence> {
  std::vector<token_sequence> out(1);
  int depth = 0;
  for (token_sequence t : r) {
    if (is(t, punctuator::left_paren) || is(t, punctuator::left_square) ||
        is(t, punctuator::left_brace))
      ++depth;
    else if (is(t, punctuator::right_paren) || is(t, punctuator::right_square) ||
             is(t, punctuator::right_brace))
      --depth;
    else if (depth == 0 && is(t, punctuator::comma)) {
      out.emplace_back();
      continue;
    }
    out.back() += t;
  }
  return out;
}

consteval void expand(token_sequence item, token_sequence prefix,
                      std::vector<token_sequence>& out) {
  auto lb = std::ranges::find_if(item, [](token_sequence t) {
    return is(t, punctuator::left_brace);
  });
  if (lb == end(item)) {
    out.push_back(prefix + item);
    return;
  }
  if (lb == begin(item) || !is(lb[-1], punctuator::colon_colon))
    std::constexpr_error_str("using_", "a group must follow '::'");
  if (!is(item[size(item) - 1], punctuator::right_brace))
    std::constexpr_error_str("using_", "nothing may follow a group");
  token_sequence head = prefix + join(std::ranges::subrange(begin(item), lb));
  for (token_sequence inner : split_commas(std::ranges::subrange(lb + 1, end(item) - 1)))
    expand(inner, head, out);
}
}  // namespace using_detail

__macro using_(std::meta::token_sequence names) {
  std::vector<std::meta::token_sequence> declared;
  for (auto item : using_detail::split_commas(names))
    using_detail::expand(item, ^^{}, declared);
  std::meta::token_sequence out = ^^{};
  for (std::size_t i = 0; i != declared.size(); ++i)
    out += (i ? ^^{ ; } : ^^{}) + ^^{ using \(declared[i]) };
  return out;
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
// expected-note@*:* 0+ {{}}
