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

// Macros and token sequences declared in a precompiled header (as clangd's
// preamble is) work as if declared in the source: a token literal keeps its
// tokens, its interpolations and the template parameters it names; a raw
// pack parameter stays raw; the token builtins (id, str_lit, tokenize) read
// back whole; expansion statements in templates (not expanded until
// instantiated) are written without expansions.

// RUN: mkdir -p %t
// RUN: %{cxx} %{compile_flags} -std=c++2d -freflection \
// RUN:     -x c++-header %s -o %t/header.pch
// RUN: %{cxx} %{compile_flags} %{link_flags} -std=c++2d -freflection \
// RUN:     -include-pch %t/header.pch %s -o %t/main.exe
// RUN: %t/main.exe

// The header half defines HEADER (which the precompiled header then carries),
// so the second compile sees only the other half.
#ifndef HEADER
#define HEADER

#include <meta>
#include <array>
#include <cassert>
#include <tuple>
#include <type_traits>

using std::meta::token_sequence;

// A raw parameter: the argument's tokens, unparsed.
__macro twice(token_sequence t) { return ^^{ (2 * ($t)) }; }

// A raw pack: the arguments split at top-level commas.
__macro count(token_sequence... args) {
  return ^^{ ${ std::meta::reflect_constant(sizeof...(args)) } };
}

// Declaration position, with a typed parameter interpolated.
__macro make_var(token_sequence name, int init) {
  return ^^{ constexpr int $name = $init; };
}

// A template parameter named in a token literal.
template <class T>
__macro declare(token_sequence name) {
  return ^^{ T $name{}; };
}

// The token builtins, and an interpolation in an injected member.
consteval auto generated() -> token_sequence {
  return ^^{ static constexpr int ${ std::meta::id("gen", 1) } = 42; }
       + std::meta::tokenize("static constexpr int gen2 = 43;");
}
consteval auto label() -> std::string_view {
  return std::meta::stringize(^^{ ${ std::meta::str_lit("hi") } });
}

struct Injected {
  consteval { queue_injection(generated()); }
};

// Expansion statements whose size depends on a template parameter.
template <class... Ts>
constexpr auto sizes() -> std::size_t {    // an init-list expansion
  std::size_t n = 0;
  template for (constexpr std::meta::info r : {^^Ts...})
    n += sizeof(typename[:r:]);
  return n;
}
template <class Tuple>
constexpr auto sum(Tuple const& t) -> int {  // a destructurable expansion
  int n = 0;
  template for (auto x : t)
    n += x;
  return n;
}
template <int N>
constexpr auto iota_sum() -> int {  // an iterable expansion
  constexpr auto r = [] {
    std::array<int, N> a{};
    for (int i = 0; i < N; ++i)
      a[i] = i;
    return a;
  }();
  int n = 0;
  template for (constexpr int x : r)
    n += x;
  return n;
}

#else

static_assert(twice!(1 + 2) == 6);
static_assert(count!() == 0);
static_assert(count!(a, b c, f(d, e)) == 3);

make_var!(seven, 7);
static_assert(seven == 7);

struct Holder {
  declare!<double>(d);
};
static_assert(std::is_same_v<decltype(Holder::d), double>);

static_assert(Injected::gen1 == 42);
static_assert(Injected::gen2 == 43);
static_assert(label() == "\"hi\"");

static_assert(sizes<char, short, int>() == 7);
static_assert(sum(std::tuple(1, 2, 3)) == 6);
static_assert(iota_sum<4>() == 6);

int main(int, char**) {
  int x = 5;
  assert(twice!(x) == 10);
  return 0;
}

#endif
