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

// A token literal ^^{ ... } is a std::meta::token_view: a read-only view of
// tokens in storage the compiler owns (static, like a string literal's). It is
// cheap to copy and never dangles; std::meta::token_sequence (a
// vector<token>) is what one builds with, and a token_view converts to one.

#include <meta>
#include <ranges>
#include <span>
#include <type_traits>

using std::meta::token;
using std::meta::token_sequence;
using std::meta::token_view;

// ------------------------------------------------------------ the type -----

static_assert(std::is_same_v<decltype(^^{ a }), token_view>);
static_assert(std::is_trivially_copyable_v<token_view>);
static_assert(!std::is_default_constructible_v<token_view>);  // only the compiler
static_assert(std::ranges::contiguous_range<token_view>);
static_assert(std::ranges::borrowed_range<token_view>);
static_assert(std::ranges::view<token_view>);

// Static storage: a constexpr token_view needs no persistent allocation.
constexpr token_view stored = ^^{ int k; };
static_assert(stored.size() == 3 && stored[1] == ^^{ k });

// ------------------------------------------------------- reading it --------

consteval bool reading() {
  auto v = ^^{ a + b };
  int idents = 0;
  for (token t : v)
    idents += token_kind_of(t) == std::meta::token_kind::identifier;
  return v.size() == 3 && !v.empty() && v.front() == ^^{ a } &&
         v.back() == ^^{ b } && *(v.begin() + 1) == ^^{ + } && idents == 2;
}
static_assert(reading());

// --------------------------------------------- building from it ------------

consteval bool building() {
  token_sequence s = ^^{ a };       // the conversion: an owned copy
  s += ^^{ + b };                   // append a literal
  s += std::meta::id("c");          // append a token
  auto v = ^^{ x };
  token_sequence w = v.to_vec();
  w.push_back(std::meta::id("y"));
  return s == ^^{ a + b c } && w == ^^{ x y } && v == ^^{ x } &&
         (^^{ p } + ^^{ q }) == ^^{ p q } && ^^{ p } + s == ^^{ p a + b c };
}
static_assert(building());

// Interpolating views, vectors, and spans of tokens.
consteval auto wrap(std::span<const token> t) -> token_view {
  return ^^{ (\(t)) };
}
consteval bool interpolation() {
  token_sequence s = ^^{ 1 + 2 };
  return wrap(^^{ a }) == ^^{ (a) } && wrap(s) == ^^{ (1 + 2) } &&
         ^^{ \(^^{ x }) \(s) } == ^^{ x 1 + 2 };
}
static_assert(interpolation());

// Literals that compare equal but differ (interpolated constants of different
// types) have their own storage.
enum class E0 { e };
enum class E1 { e };
consteval auto case_of(std::meta::info e) -> token_view {
  return ^^{ \(e) };
}
template <class E>
consteval bool same_enum() {
  token_view v = case_of(enumerators_of(^^E)[0]);
  queue_injection(^^{ constexpr auto \(std::meta::id(identifier_of(^^E), "_value")) = \(v); });
  return true;
}
consteval {
  same_enum<E0>();
  same_enum<E1>();
}
static_assert(std::is_same_v<decltype(E0_value), const E0>);
static_assert(std::is_same_v<decltype(E1_value), const E1>);

// ------------------------------------------------------- raw parameters ----

// Only read: a token_view, the same object every time it is mentioned.
__macro count(token_view names) {
  int n = 0;
  for (auto b = names.begin(), e = names.end(); b != e; ++b)
    ++n;
  return ^^{ \(std::meta::reflect_constant(n)) };
}
static_assert(count!(a b c) == 3);

// Modified: a token_sequence, an owned copy.
__macro plus_one(token_sequence names) {
  names.push_back(std::meta::id("z"));
  return ^^{ \(std::meta::reflect_constant(int(names.size()))) };
}
static_assert(plus_one!(a b) == 3);

// A pack of views.
__macro sizes(token_view... args) {
  int total = 0;
  ((total = total * 10 + int(args.size())), ...);
  return ^^{ \(std::meta::reflect_constant(total)) };
}
static_assert(sizes!(a, b c, d e f) == 123);

int main(int, char**) { return 0; }
