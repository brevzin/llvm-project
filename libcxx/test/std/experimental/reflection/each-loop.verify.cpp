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

// each-loop.pass.cpp's header split: a header without a top-level ':' is
// diagnosed.

#include <meta>
#include <debugging>
#include <vector>

__macro each(std::meta::token_sequence head, std::meta::token_sequence body) {
  auto [elem, range] = head.balanced_split_at(std::meta::punctuator::colon);
  if (range.empty())
    std::consteval_error_str("each", "expected 'declaration : range'");
  return ^^{ for (\(elem) : \(range)) { \(body) } };
}

void f(std::vector<int> v) {
  each!(int i : v, (void)i;); // OK

  each!(int i, v, (void)i;);
  // expected-error@-1 {{macro 'each' reported an error}}
  // expected-note@*:* {{constexpr message with tag 'each': expected 'declaration : range'}}

  each!(int i :, (void)i;);
  // expected-error@-1 {{macro 'each' reported an error}}
  // expected-note@*:* {{constexpr message with tag 'each': expected 'declaration : range'}}

  each!(int i [: ^^int : ^^long :], (void)i;);
  // expected-error@-1 {{macro 'each' reported an error}}
  // expected-note@*:* {{constexpr message with tag 'each': expected 'declaration : range'}}
}
