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
// ADDITIONAL_COMPILE_FLAGS: -std=c++2d

// P3549: std::visit ignores visitor arms that return noreturn_t when
// determining its result type. The remaining arms must agree on a single type,
// which is the result; if every arm diverges, the result is noreturn_t.

#include <cassert>
#include <cstddef>
#include <exception>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "test_macros.h"

struct Login {};
struct Connected {};
struct Disconnected {};

struct Error {};
[[noreturn]] void throw_error() { throw Error{}; }

template <class T, class... Ts>
constexpr bool is_one_of = (std::is_same_v<T, Ts> || ...);

// A visitor whose "impossible state" arms only throw (the pattern that used to
// fail: those arms deduce noreturn_t while the others return void).
void void_with_throwing_arms() {
  std::variant<Login, Connected, Disconnected> state = Disconnected{};
  bool connected = false;
  auto visitor   = [&]<class S>(S const&) {
    if constexpr (is_one_of<S, Login, Connected>)
      throw_error();
    else
      connected = true;
  };
  static_assert(std::is_same_v<decltype(visitor(Login{})), std::noreturn_t>);
  static_assert(std::is_same_v<decltype(visitor(Disconnected{})), void>);
  static_assert(std::is_same_v<decltype(std::visit(visitor, state)), void>);
  std::visit(visitor, state);
  assert(connected);
#ifndef TEST_HAS_NO_EXCEPTIONS
  state = Login{};
  try {
    std::visit(visitor, state);
    assert(false);
  } catch (Error) {
  }
#endif
}

// An if constexpr branch ending in std::terminate() (which returns noreturn_t).
void value_with_terminating_branch() {
  std::variant<int, std::string> v = 42;
  auto visitor                     = [](auto const& x) {
    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, int>)
      return x + 1;
    else
      std::terminate();
  };
  static_assert(std::is_same_v<decltype(std::visit(visitor, v)), int>);
  assert(std::visit(visitor, v) == 43);
}

// The result can be a reference.
void reference_result() {
  int target                = 0;
  std::variant<int, long> v = 1;
  auto visitor              = [&](auto x) -> decltype(auto) {
    if constexpr (std::is_same_v<decltype(x), int>)
      return (target);
    else
      std::unreachable();
  };
  static_assert(std::is_same_v<decltype(std::visit(visitor, v)), int&>);
  std::visit(visitor, v) = 7;
  assert(target == 7);
}

// A non-movable result is still returned by guaranteed copy elision.
struct NonMovable {
  int value;
  explicit NonMovable(int v) : value(v) {}
  NonMovable(NonMovable&&) = delete;
};

void non_movable_result() {
  std::variant<int, char> v = 5;
  auto visitor              = [](auto x) {
    if constexpr (std::is_same_v<decltype(x), int>)
      return NonMovable(x);
    else
      std::terminate();
  };
  NonMovable nm = std::visit(visitor, v);
  assert(nm.value == 5);
}

// If every arm diverges, so does the visit.
void all_arms_diverge() {
  std::variant<int, long> v = 1;
  auto visitor              = [](auto) { throw_error(); };
  static_assert(std::is_same_v<decltype(std::visit(visitor, v)), std::noreturn_t>);
#ifndef TEST_HAS_NO_EXCEPTIONS
  try {
    std::visit(visitor, v);
    assert(false);
  } catch (Error) {
  }
#endif
  // A diverging visit converts to anything, like any diverging expression.
  auto f = [&](bool b) -> std::string { return b ? std::string("no") : std::visit(visitor, v); };
  assert(f(true) == "no");
}

// Multiple variants: every combination of alternatives is an arm.
void multiple_variants() {
  std::variant<int, std::string> a = 3;
  std::variant<int, double> b      = 4;
  auto visitor                     = [](auto const& x, auto const& y) {
    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, int> && std::is_same_v<std::decay_t<decltype(y)>, int>)
      return x * y;
    else
      std::unreachable();
  };
  static_assert(std::is_same_v<decltype(std::visit(visitor, a, b)), int>);
  assert(std::visit(visitor, a, b) == 12);
}

// The value category of each variant is forwarded to the arms.
void value_categories() {
  std::variant<int, long> v = 1;
  auto visitor              = []<class T>(T&&) {
    if constexpr (std::is_same_v<T, int>) // rvalue
      return 1;
    else if constexpr (std::is_same_v<T, int&>)
      return 2;
    else if constexpr (std::is_same_v<T, const int&>)
      return 3;
    else
      std::unreachable();
  };
  const auto& cv = v;
  assert(std::visit(visitor, std::move(v)) == 1);
  assert(std::visit(visitor, v) == 2);
  assert(std::visit(visitor, cv) == 3);
}

// Member visit, and a type derived from variant.
struct Derived : std::variant<int, std::string> {
  using variant::variant;
};

void member_and_derived() {
  auto visitor = [](auto const& x) {
    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, int>)
      return x;
    else
      std::terminate();
  };
  std::variant<int, std::string> v = 9;
  assert(v.visit(visitor) == 9);
  Derived d = 10;
  assert(std::visit(visitor, d) == 10);
}

// Usable in constant expressions, as long as no diverging arm is taken.
constexpr int constant_evaluation() {
  std::variant<int, long> v = 11;
  return std::visit(
      [](auto x) {
        if constexpr (std::is_same_v<decltype(x), int>)
          return x;
        else
          std::unreachable();
      },
      v);
}
static_assert(constant_evaluation() == 11);

// Visitors without noreturn_t arms behave as before.
void unchanged_without_noreturn() {
  std::variant<int, long> v = 2;
  static_assert(std::is_same_v<decltype(std::visit([](auto x) { return static_cast<long>(x); }, v)), long>);
  static_assert(std::is_same_v<decltype(std::visit([](auto) {}, v)), void>);
  assert(std::visit([](auto x) { return static_cast<long>(x) * 2; }, v) == 4);
  // visit<R> is unaffected.
  assert(std::visit<long>([](auto x) { return x; }, v) == 2);
}

int main(int, char**) {
  void_with_throwing_arms();
  value_with_terminating_branch();
  reference_result();
  non_movable_result();
  all_arms_diverge();
  multiple_variants();
  value_categories();
  member_and_derived();
  unchanged_without_noreturn();
  return 0;
}
