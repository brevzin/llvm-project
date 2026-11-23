//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection

// <meta>
//
// [meta.reflection.scope] current_function and current_class throw when there
// is no such entity.

#include <meta>

namespace N {
constexpr auto f = std::meta::current_function();
// expected-error@-1 {{must be initialized by a constant expression}}
// expected-note@*:* {{subexpression not valid in a constant expression}}
// expected-note@-3 {{in call to}}

struct S {
  static constexpr auto f2 = std::meta::current_function();
  // expected-error@-1 {{must be initialized by a constant expression}}
  // expected-note@*:* {{subexpression not valid in a constant expression}}
  // expected-note@-3 {{in call to}}
};

constexpr auto c = std::meta::current_class();
// expected-error@-1 {{must be initialized by a constant expression}}
// expected-note@*:* {{subexpression not valid in a constant expression}}
// expected-note@-3 {{in call to}}

void fn() {
  constexpr auto c2 = std::meta::current_class();
  // expected-error@-1 {{must be initialized by a constant expression}}
  // expected-note@*:* {{subexpression not valid in a constant expression}}
  // expected-note@-3 {{in call to}}
}
}  // namespace N
