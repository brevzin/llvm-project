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

// token_sequence::find, balanced_find, split_at, and balanced_split_at, each
// taking a token sequence, a token, or a punctuator.

#include <meta>

using std::meta::punctuator;
using std::meta::token;
using std::meta::token_sequence;

constexpr auto npos = token_sequence::npos;

// ---------------------------------------------------------------- find ----

constexpr token_sequence ts = ^^{ a + b : c + d };
//                                0 1 2 3 4 5 6

// A punctuator, a token, or a one-token sequence.
static_assert(ts.find(punctuator::plus) == 1);
static_assert(ts.find(^^{ + }) == 1);
static_assert(ts.find((^^{ + })[0]) == 1);
static_assert(ts.find(ts[3]) == 3);

// From a position.
static_assert(ts.find(punctuator::plus, 2) == 5);
static_assert(ts.find(punctuator::plus, 6) == npos);
static_assert(ts.find(punctuator::plus, 7) == npos);   // at the end
static_assert(ts.find(punctuator::plus, 100) == npos); // past it

// Not there.
static_assert(ts.find(punctuator::minus) == npos);
static_assert(ts.find(^^{ e }) == npos);
static_assert(token_sequence().find(punctuator::plus) == npos);

// Identifiers, keywords, and literals compare by spelling.
static_assert(ts.find(^^{ c }) == 4);
static_assert(ts.find(std::meta::id("d")) == 6);
static_assert((^^{ int x = 42; return 42; }).find(^^{ return }) == 5);
static_assert((^^{ int x = 42; return 42; }).find(^^{ 42 }, 4) == 6);
static_assert((^^{ f("x") + f("y") }).find(^^{ "y" }) == 7);

// A sequence: all of its tokens, in order.
static_assert(ts.find(^^{ : c }) == 3);
static_assert(ts.find(^^{ c + d }) == 4);
static_assert(ts.find(^^{ + c }) == npos);
static_assert(ts.find(^^{ c + d e }) == npos);         // runs off the end
static_assert((^^{ else else if }).find(^^{ else if }) == 1);

// The empty sequence occurs anywhere.
static_assert(ts.find(token_sequence()) == 0);
static_assert(ts.find(token_sequence(), 3) == 3);
static_assert(ts.find(token_sequence(), 7) == 7);
static_assert(ts.find(token_sequence(), 8) == npos);

// A digraph or alternative token is the same punctuator as its primary
// spelling.
static_assert((^^{ x <% y %> }).find(punctuator::left_brace) == 1);
static_assert((^^{ x <% y %> }).find((^^{ { } })[1]) == 3);
static_assert((^^{ a and b }).find(punctuator::ampersand_ampersand) == 1);
static_assert((^^{ a && b }).find(^^{ and }) == 1);

// Annotation tokens (interpolated values) compare as tokens_equal does.
constexpr int one = 1;
static_assert((^^{ f(\(one), \(2)) }).find(^^{ \(1) }) == 2);
static_assert((^^{ f(\(one), \(2)) }).find(^^{ \(2) }) == 4);
static_assert((^^{ f(\(one), \(2)) }).find(^^{ \(3) }) == npos);

// ------------------------------------------------------- balanced_find ----

constexpr token_sequence nested = ^^{ f ( a , b ) , [ c , d ] , { e , g } , h };
//                                    0 1 2 3 4 5 6 7 8 9 10 11 12 ...

// Only outside brackets.
static_assert(nested.find(punctuator::comma) == 3);
static_assert(nested.balanced_find(punctuator::comma) == 6);
static_assert(nested.balanced_find(punctuator::comma, 7) == 12);
static_assert(nested.balanced_find(^^{ , }, 13) == 18);
static_assert(nested.balanced_find(^^{ h }) == 19);
static_assert(nested.balanced_find(^^{ c }) == npos);

// From inside a group: within it, ending at its closer.
static_assert(nested.balanced_find(punctuator::comma, 2) == 3);
static_assert(nested.balanced_find(punctuator::comma, 4) == npos);
static_assert(nested.balanced_find(^^{ h }, 2) == npos);

// Just after an opener, its closer.
static_assert(nested.balanced_find(punctuator::right_paren, 2) == 5);
static_assert(nested.balanced_find(punctuator::right_square, 8) == 11);
static_assert((^^{ ( ( x ) y ) z }).balanced_find(punctuator::right_paren, 1) ==
              5);

// An opener itself, at the top level.
static_assert(nested.balanced_find(punctuator::left_square) == 7);
static_assert(nested.balanced_find(punctuator::left_paren, 2) == npos);

// Splices nest too; <> doesn't.
static_assert((^^{ [: c ? x : y :] : r }).balanced_find(punctuator::colon) ==
              7);
static_assert((^^{ A<b, c> , d }).balanced_find(punctuator::comma) == 3);

// A sequence counts where it starts at the top level.
static_assert((^^{ ( a b ) a b }).balanced_find(^^{ a b }) == 4);
static_assert((^^{ ( ) a }).balanced_find(^^{ ( ) }) == 0);

// ------------------------------------------------------------ split_at ----

consteval bool splits(std::pair<token_sequence, token_sequence> p,
                      token_sequence first, token_sequence second) {
  return p.first == first && p.second == second;
}

static_assert(splits(ts.split_at(punctuator::colon), ^^{ a + b }, ^^{ c + d }));
static_assert(splits(ts.split_at(^^{ : }), ^^{ a + b }, ^^{ c + d }));
static_assert(splits(ts.split_at(ts[3]), ^^{ a + b }, ^^{ c + d }));
static_assert(splits(ts.split_at(^^{ b : c }), ^^{ a + }, ^^{ + d }));

// The first occurrence.
static_assert(splits(ts.split_at(punctuator::plus), ^^{ a }, ^^{ b : c + d }));

// None: everything, then nothing (as when the separator ends the sequence).
static_assert(splits(ts.split_at(punctuator::minus), ts, token_sequence()));
static_assert(splits(ts.split_at(^^{ d }), ^^{ a + b : c + }, token_sequence()));
static_assert(splits(token_sequence().split_at(punctuator::colon),
                     token_sequence(), token_sequence()));

// At the start.
static_assert(splits(ts.split_at(^^{ a }), token_sequence(), ^^{ + b : c + d }));

// --------------------------------------------------- balanced_split_at ----

// A range-for header: the declaration has no top-level ':'; the range may.
constexpr token_sequence header = ^^{ auto [k, v] : c ? m : n };
static_assert(splits(header.balanced_split_at(punctuator::colon),
                     ^^{ auto [k, v] }, ^^{ c ? m : n }));
static_assert(splits(header.balanced_split_at(^^{ : }),
                     ^^{ auto [k, v] }, ^^{ c ? m : n }));
static_assert(splits(header.balanced_split_at(header[6]),
                     ^^{ auto [k, v] }, ^^{ c ? m : n }));

static_assert(splits(nested.balanced_split_at(punctuator::comma),
                     ^^{ f(a, b) }, ^^{ [c, d], {e, g}, h }));
static_assert(splits(nested.balanced_split_at(^^{ , h }),
                     ^^{ f(a, b), [c, d], {e, g} }, token_sequence()));

// None at the top level.
static_assert(splits((^^{ f(a, b) }).balanced_split_at(punctuator::comma),
                     ^^{ f(a, b) }, token_sequence()));

int main() {}
