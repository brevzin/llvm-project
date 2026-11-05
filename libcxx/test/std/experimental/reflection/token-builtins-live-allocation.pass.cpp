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

// str_lit, tokenize, and id evaluate their arguments as part of the enclosing
// evaluation: an allocation that is live at the call (a vector, a new) is not
// a leak. (They used to evaluate a variable argument -- a pointer, a char, an
// integer -- as if it were a complete constant expression, which reported
// any live allocation as "not deallocated".)

#include <meta>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

// A pointer variable.
consteval auto str_lit_of_pointer() -> bool {
  std::vector<int> v{1, 2};
  const char* p = "xy";
  auto t = std::meta::str_lit(p);
  return stringize(std::meta::token_sequence{t}) == R"("xy")"sv && v.size() == 2;
}
static_assert(str_lit_of_pointer());

consteval auto tokenize_pointer() -> bool {
  int* q = new int(3);
  const char* p = "a + b";
  auto ts = std::meta::tokenize(p);
  bool ok = ts.size() == 3 && *q == 3;
  delete q;
  return ok;
}
static_assert(tokenize_pointer());

// A char variable and an integer variable.
consteval auto id_of_char_and_int() -> bool {
  std::vector<int> v{1};
  char c = 'x';
  int n = 42;
  return std::meta::id(c, n) == std::meta::id("x42") && v.size() == 1;
}
static_assert(id_of_char_and_int());

// A pointer into a string literal, offset.
consteval auto str_lit_of_offset_pointer() -> bool {
  std::vector<int> v{1};
  const char* p = "hello";
  p += 2;
  return stringize(std::meta::token_sequence{std::meta::str_lit(p)}) ==
             R"("llo")"sv &&
         v.size() == 1;
}
static_assert(str_lit_of_offset_pointer());

// Where this came up: a macro that walks a function's annotations (a live
// vector) and turns each one's string into tokens.
struct clause {
  const char* cond;
};

// Not a constant expression: a violated clause fails constant evaluation.
inline void violated(const char*) {}

__macro check() {
  std::meta::token_sequence out;
  for (std::meta::info a : annotations_of(std::meta::macro_expansion_context(),
                                          ^^clause)) {
    auto c = extract<clause>(a);
    auto cond = std::meta::tokenize(c.cond);
    auto spelled = std::meta::str_lit(c.cond);
    out += ^^{ if (!(\(cond))) ::violated(\(spelled)); };
  }
  return out;
}

[[=clause{"x > 0"}]] [[=clause{"x < 10"}]]
constexpr auto checked(int x) -> int {
  check!();
  return x;
}
static_assert(checked(5) == 5);

int main() {}
