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

// std::meta::unique_id(ts...): a fresh identifier, 'prefix#N', which nothing
// written in source or made by id() can spell -- for the names a token
// sequence declares for itself. Numbered per enclosing definition: the
// outermost function (a lambda's names belong to what encloses it), else the
// class, else the translation unit.

#include <meta>
#include <cassert>
#include <string_view>

using std::meta::token_sequence;

// ------------------------------------------------------------ the token ----

consteval bool basics() {
  auto a = std::meta::unique_id("a");
  auto b = std::meta::unique_id("a");
  return token_kind_of(a) == std::meta::token_kind::identifier &&
         identifier_of(a).starts_with("a#") && a != b &&
         a != std::meta::id("a") && stringize(a) == identifier_of(a) &&
         identifier_of(std::meta::unique_id("p", 2, "q")).starts_with("p2q#");
}
static_assert(basics());

// ------------------------------------------------------------ numbering ----

// The spelling of a fresh name made where the macro is expanded. (The name is
// made once, into a variable: an expression that depends on nothing, like
// str_lit(identifier_of(unique_id("x"))), may be evaluated where it is
// written, or more than once.)
__macro fresh_name() {
  auto n = std::meta::unique_id("x");
  return ^^{ ${std::meta::str_lit(identifier_of(n))} };
}
template <class T>
__macro fresh_name_of(T&&) {
  auto n = std::meta::unique_id("x");
  return ^^{ ${std::meta::str_lit(identifier_of(n))} };
}

void numbered() {
  assert(std::string_view(fresh_name!()) == "x#1");
  assert(std::string_view(fresh_name!()) == "x#2");
  auto l = [] { return std::string_view(fresh_name!()); };
  assert(l() == "x#3");  // the lambda's names are the function's
}

void numbered_again() {
  assert(std::string_view(fresh_name!()) == "x#1");  // its own numbers
}

struct S {
  static constexpr std::string_view a = fresh_name!();
  static constexpr std::string_view b = fresh_name!();
};
static_assert(S::a == "x#1" && S::b == "x#2");

// Each specialization is its own definition, whatever the order in which
// they are instantiated.
template <class T>
std::string_view in_template(T t) {
  return fresh_name_of!(t);
}

// ----------------------------------------------------------- no capture ----

// Its own loop variable: neither the body's names nor another expansion's.
__macro twice(token_sequence body) {
  auto i = std::meta::unique_id("i");
  return ^^{ for (int $i = 0; $i != 2; ++$i) { $body } };
}

void no_capture() {
  int i = 0, total = 0;
  twice!(twice!(total += 1;); i += 10;);
  assert(total == 4 && i == 20);
}

// Labels are function-wide: two expansions in one function need two.
__macro skip_if(bool c, token_sequence body) {
  auto l = std::meta::unique_id("skip");
  return ^^{ if ($c) goto $l; $body $l:; };
}

void labels(bool yes) {
  int n = 0;
  skip_if!(yes, n += 1;);
  skip_if!(!yes, n += 2;);
  assert(n == 2);
}

// A static local of its own (the name is mangled into the variable's symbol).
__macro bump(int& out) {
  auto s = std::meta::unique_id("calls");
  return ^^{ static int $s = 0; $out = ++$s };
}

inline int calls() {
  int a = 0, b = 0;
  bump!(a);
  bump!(b);
  bump!(b);
  return a * 10 + b;
}

int main(int, char**) {
  numbered();
  numbered_again();
  assert(in_template(1L) == "x#1");
  assert(in_template(1) == "x#1");
  no_capture();
  labels(true);
  assert(calls() == 11);
  assert(calls() == 22);
  return 0;
}
