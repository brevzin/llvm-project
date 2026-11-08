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

// A template parameter holding a reflection that no reflect-expression can
// produce (a null reflection, a direct base class relationship, a data member
// description, an annotation) can be substituted more than once: here, a
// concept-id that is not satisfied is checked again inside another template.

#include <meta>
#include <type_traits>

using std::meta::info;

struct B {};
struct D : B {};
[[= 1]] int annotated;

consteval info fail(info r) {
  if (true)
    throw 0;
  return r;
}

template <info R>
concept C = requires { typename std::integral_constant<info, fail(R)>; };

template <info R, bool Expected>
consteval bool check() {
  static_assert(C<R> == Expected);
  return true;
}

static_assert(check<info{}, false>());
static_assert(check<bases_of(^^D, std::meta::access_context::unchecked())[0],
                    false>());
static_assert(check<data_member_spec(^^int, {.name = "m"}), false>());
static_assert(check<annotations_of(^^annotated)[0], false>());
static_assert(check<std::meta::reflect_constant(42), false>());

int main() {}
