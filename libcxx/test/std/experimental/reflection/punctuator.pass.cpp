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

// std::meta::punctuator: what a punctuation token is (punctuator_of), its
// spelling (symbol_of), and -- by interpolating a value, as for
// std::meta::operators -- the token itself.

#include <meta>
#include <string_view>
#include <vector>

using std::meta::punctuator;
using std::meta::token_sequence;

consteval bool round_trips() {
  for (auto e : enumerators_of(^^std::meta::punctuator)) {
    punctuator p = extract<punctuator>(constant_of(e));
    token_sequence t = ^^{ \(p) };
    if (tokens_of(t).size() != 1)
      return false;
    if (punctuator_of(t) != p)
      return false;
    if (std::string_view(stringize(t)) != symbol_of(p))
      return false;
    if (u8symbol_of(p).size() != symbol_of(p).size())
      return false;
  }
  return true;
}
static_assert(round_trips());
static_assert(enumerators_of(^^std::meta::punctuator).size() == 55);

// Tokens from source.
static_assert(punctuator_of(^^{ ; }) == punctuator::semicolon);
static_assert(punctuator_of(^^{ :: }) == punctuator::colon_colon);
static_assert(punctuator_of(^^{ ... }) == punctuator::ellipsis);
static_assert(punctuator_of(^^{ ->* }) == punctuator::arrow_star);
static_assert(punctuator_of(^^{ <=> }) == punctuator::spaceship);
static_assert(punctuator_of(^^{ ^^ }) == punctuator::caret_caret);
static_assert(punctuator_of(^^{ ## }) == punctuator::hash_hash);
static_assert(punctuator_of(tokens_of(^^{ [: :] })[0]) == punctuator::left_splice);
static_assert(punctuator_of(tokens_of(^^{ [: :] })[1]) == punctuator::right_splice);
static_assert(punctuator_of(tokens_of(^^{ { } })[0]) == punctuator::left_brace);
static_assert(punctuator_of(tokens_of(^^{ { } })[1]) == punctuator::right_brace);

// A digraph or alternative token is its primary spelling's punctuator.
static_assert(punctuator_of(tokens_of(^^{ <% %> })[0]) == punctuator::left_brace);
static_assert(punctuator_of(tokens_of(^^{ <% %> })[1]) == punctuator::right_brace);
static_assert(punctuator_of(^^{ and }) == punctuator::ampersand_ampersand);
static_assert(punctuator_of(^^{ bitor }) == punctuator::pipe);
static_assert(punctuator_of(^^{ not_eq }) == punctuator::exclamation_equals);

// A lone brace, which a token literal cannot spell.
static_assert(^^{ \(punctuator::left_brace) } == tokens_of(^^{ { } })[0]);
static_assert(^^{ \(punctuator::right_brace) } == tokens_of(^^{ { } })[1]);

// Every operator with a one-token spelling has the punctuator of the same
// name (without "op_"), and the two agree on the token.
consteval bool agrees_with_operators() {
  for (auto op : enumerators_of(^^std::meta::operators)) {
    std::string_view name = identifier_of(op);
    name.remove_prefix(3);  // "op_"
    bool found = false;
    for (auto p : enumerators_of(^^std::meta::punctuator)) {
      if (identifier_of(p) != name)
        continue;
      found = true;
      auto o = extract<std::meta::operators>(constant_of(op));
      auto q = extract<punctuator>(constant_of(p));
      if (symbol_of(o) != symbol_of(q))
        return false;
      if (^^{ \(o) } != ^^{ \(q) })
        return false;
    }
    // Only the operators that are not a single punctuator lack one.
    bool multi = name == "new" || name == "delete" || name == "array_new" ||
                 name == "array_delete" || name == "co_await" ||
                 name == "parentheses" || name == "square_brackets";
    if (found == multi)
      return false;
  }
  return true;
}
static_assert(agrees_with_operators());

static_assert(symbol_of(std::meta::op_caret_equals) == "^=");
static_assert(symbol_of(std::meta::op_co_await) == "co_await");
static_assert(u8symbol_of(std::meta::op_caret_equals) == u8"^=");

// Splitting at top-level commas, with brackets of every kind nesting.
consteval auto split(token_sequence ts) -> std::vector<token_sequence> {
  std::vector<token_sequence> out(1, ^^{});
  int depth = 0;
  for (token_sequence t : tokens_of(ts)) {
    if (token_kind_of(t) == std::meta::token_kind::punctuator) {
      switch (punctuator_of(t)) {
      case punctuator::left_paren:
      case punctuator::left_square:
      case punctuator::left_brace:
        ++depth;
        break;
      case punctuator::right_paren:
      case punctuator::right_square:
      case punctuator::right_brace:
        --depth;
        break;
      case punctuator::comma:
        if (depth == 0) {
          out.push_back(^^{});
          continue;
        }
        break;
      default:
        break;
      }
    }
    out.back() += t;
  }
  return out;
}

static_assert(split(^^{ a, f(b, c), S{d, e}, [g, h] {}, i }).size() == 5);
static_assert(split(^^{ a, f(b, c), S{d, e}, [g, h] {}, i })[2] == ^^{ S{d, e} });

int main(int, char**) { return 0; }
