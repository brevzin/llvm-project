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

// Qualified macro invocations in declaration and statement position: errors,
// each diagnosed once.

#include <meta>

namespace lib {
__macro m() { return ^^{}; }
int not_a_macro;
} // namespace lib

namespace A {
lib::nope!();        // expected-error {{use of undeclared expression macro 'nope'}}
nosuch::m!();        // expected-error {{use of undeclared identifier 'nosuch'}}
lib::not_a_macro!(); // expected-error {{'not_a_macro' is not an expression macro}}
} // namespace A

void f() {
  lib::nope!(); // expected-error {{use of undeclared expression macro 'nope'}}
  nosuch::m!(); // expected-error {{use of undeclared identifier 'nosuch'}}
}
