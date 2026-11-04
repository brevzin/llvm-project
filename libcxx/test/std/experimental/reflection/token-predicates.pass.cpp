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

// Token predicates: is_identifier, is_keyword, is_literal, is_punctuator,
// is_unique_id, and comparing a token with a punctuator.

#include <meta>

using std::meta::punctuator;
using std::meta::token;
using std::meta::token_sequence;

constexpr token_sequence ts = ^^{ x int 42 "s" + { } };
//                                0 1   2  3   4 5 6

// ------------------------------------------------------------ the kinds ----

static_assert(is_identifier(ts[0]));
static_assert(!is_identifier(ts[1]) && !is_identifier(ts[2]) &&
              !is_identifier(ts[4]));

static_assert(is_keyword(ts[1]));
static_assert(!is_keyword(ts[0]) && !is_keyword(ts[2]) && !is_keyword(ts[4]));

static_assert(is_literal(ts[2]) && is_literal(ts[3]));
static_assert(!is_literal(ts[0]) && !is_literal(ts[1]) && !is_literal(ts[4]));

static_assert(is_punctuator(ts[4]) && is_punctuator(ts[5]) &&
              is_punctuator(ts[6]));
static_assert(!is_punctuator(ts[0]) && !is_punctuator(ts[1]) &&
              !is_punctuator(ts[2]));

// An alternative token is the punctuator it stands for, not a keyword.
static_assert(is_punctuator((^^{ and })[0]) && !is_keyword((^^{ and })[0]));

// No token is of any kind.
static_assert(!is_identifier(token()) && !is_keyword(token()) &&
              !is_literal(token()) && !is_punctuator(token()));

// --------------------------------------------------------- is_unique_id ----

constexpr token u = std::meta::unique_id("e");
static_assert(is_identifier(u));
static_assert(is_unique_id(u));
static_assert(is_unique_id(std::meta::unique_id("e", 2)));

// Any other identifier is not, however it is made.
static_assert(!is_unique_id(ts[0]));
static_assert(!is_unique_id(std::meta::id("e")));
static_assert(!is_unique_id(std::meta::id("e", 1)));

// Nor is any other token.
static_assert(!is_unique_id(ts[1]) && !is_unique_id(ts[2]) &&
              !is_unique_id(ts[4]) && !is_unique_id(token()));

// A unique_id interpolated into a sequence is still one.
static_assert(is_unique_id((^^{ auto&& \(u) = x; })[2]));

// --------------------------------------------------- token == punctuator ----

static_assert(ts[4] == punctuator::plus);
static_assert(ts[5] == punctuator::left_brace);  // ^^{ { } can't spell this
static_assert(ts[6] == punctuator::right_brace);
static_assert(ts[4] != punctuator::minus);

// Either order.
static_assert(punctuator::plus == ts[4]);
static_assert(punctuator::minus != ts[4]);

// A token that isn't a punctuator is no punctuator.
static_assert(ts[0] != punctuator::plus);
static_assert(ts[1] != punctuator::colon);
static_assert(ts[2] != punctuator::plus);
static_assert(token() != punctuator::plus);

// Digraphs and alternative tokens are their primary punctuators.
static_assert((^^{ <% %> })[0] == punctuator::left_brace);
static_assert((^^{ and })[0] == punctuator::ampersand_ampersand);
static_assert((^^{ :: })[0] == punctuator::colon_colon);
static_assert((^^{ :: })[0] != punctuator::colon);

// Usable while scanning.
consteval auto count_braces(token_sequence body) -> int {
  int n = 0;
  for (token t : body)
    if (t == punctuator::left_brace || t == punctuator::right_brace)
      ++n;
  return n;
}
static_assert(count_braces(^^{ if (a) { b; } else { { c; } } }) == 6);

int main() {}
