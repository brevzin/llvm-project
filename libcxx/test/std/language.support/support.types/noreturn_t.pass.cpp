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

// P3549: std::noreturn_t, the type of a diverging expression, and the library
// functions that return it.

#include <cassert>
#include <cstddef>
#include <exception>
#include <expected>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

static_assert(__cpp_diverging_expressions);
static_assert(std::is_same_v<std::noreturn_t, decltype(throw 0)>);

// The type.
static_assert(std::is_noreturn_v<std::noreturn_t>);
static_assert(std::is_noreturn_v<const volatile std::noreturn_t>);
static_assert(!std::is_noreturn_v<void>);
static_assert(!std::is_noreturn_v<std::noreturn_t&>);
static_assert(std::is_fundamental_v<std::noreturn_t>);
static_assert(std::is_object_v<std::noreturn_t>);
static_assert(!std::is_scalar_v<std::noreturn_t>);
static_assert(!std::is_void_v<std::noreturn_t>);
static_assert(std::is_trivially_copyable_v<std::noreturn_t>);
static_assert(!std::is_default_constructible_v<std::noreturn_t>);
static_assert(std::is_copy_constructible_v<std::noreturn_t>);

// It converts to anything...
static_assert(std::is_convertible_v<std::noreturn_t, int>);
static_assert(std::is_convertible_v<std::noreturn_t, std::string>);
static_assert(std::is_convertible_v<std::noreturn_t, int&>);
static_assert(std::is_convertible_v<std::noreturn_t, void>);
// ... and nothing converts to it.
static_assert(!std::is_convertible_v<int, std::noreturn_t>);

// The common type is the other type; this is what lets a range declare
// 'using value_type = std::noreturn_t'.
static_assert(std::is_same_v<std::common_type_t<int, std::noreturn_t>, int>);
static_assert(std::is_same_v<std::common_reference_t<int&, std::noreturn_t&>, int&>);

// The library's non-returning functions return it.
static_assert(std::is_same_v<decltype(std::unreachable()), std::noreturn_t>);
static_assert(std::is_same_v<decltype(std::terminate()), std::noreturn_t>);

int pick(int i) {
  switch (i) {
  case 0:
    return 1;
  default:
    return std::unreachable();
  }
}

std::string name(int i) { return i == 0 ? "zero" : std::unreachable(); }

int main(int, char**) {
  // A function returning noreturn_t is a function returning void, as far as
  // calling it is concerned.
  std::terminate_handler old = std::set_terminate(std::terminate);
  std::set_terminate(old);

  std::function<int()> f = [] { std::terminate(); };
  std::function<void()> g = [] { std::terminate(); };
  (void)f;
  (void)g;

  std::variant<int, std::string> v = 2;
  int r = std::visit(
      [](auto& x) -> int {
        if constexpr (std::is_same_v<std::decay_t<decltype(x)>, int>)
          return x;
        else
          std::unreachable();
      },
      v);

  assert(pick(0) == 1);
  assert(name(0) == "zero");
  assert(r == 2);
  return 0;
}
