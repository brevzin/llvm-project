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

// constant_of and is_constant_expression on an argument that was converted to
// the parameter's type: a string literal bound to a std::string_view
// parameter. The conversion is evaluated as a template argument would be
// (manifestly constant-evaluated), so it is a constant expression.

#include <meta>
#include <string_view>

__macro len(std::string_view s) {
  if (!is_constant_expression(s))
    return ^^{ -1 };
  std::string_view v = extract<std::string_view>(constant_of(s));
  return ^^{ \(std::meta::reflect_constant(int(v.size()))) };
}

template <class T>
__macro first_char(std::string_view s, T&&) {
  return ^^{ \(std::meta::reflect_constant(extract<std::string_view>(constant_of(s))[0])) };
}

static_assert(len!("abc") == 3);
static_assert(len!("") == 0);
constexpr std::string_view sv = "hello";
static_assert(len!(sv) == 5);
static_assert(first_char!("xyz", 1) == 'x');

int main() {
  const char* p = "runtime";
  static_assert(len!(p) == -1);   // not a constant expression
}
