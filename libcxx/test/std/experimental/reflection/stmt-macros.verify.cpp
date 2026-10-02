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

// Statement macros: arguments are evaluated once across all the statements of
// the expansion, and an invocation takes no attributes.

#include <meta>

__macro twice(int x) {
  return ^^{ (void)\(x); (void)\(x) };
}

__macro nothing() { return ^^{}; }

void evaluated_twice(int a) {
  twice!(a); // expected-error {{expansion of expression macro would evaluate this argument more than once}}
}

template <class T>
void evaluated_twice_dependent(T a) {
  twice!(a); // expected-error {{expansion of expression macro would evaluate this argument more than once}}
}
template void evaluated_twice_dependent(int); // expected-note {{in instantiation of}}

// A statement sequence is a statement macro's expansion only where the
// invocation is a whole statement; in expression position, an expansion must
// still form exactly one expression.
__macro two_statements(int& x, int& y) {
  return ^^{ ++\(x); ++\(y) };
}

void one_expression(int a, int b) {
  two_statements!(a, b);              // fine: a whole statement
  int c = two_statements!(a, b);      // expected-error {{expansion of expression macro must form a single expression}}
  two_statements!(a, b), (void)c;     // expected-error {{expansion of expression macro must form a single expression}}
}

void attributes() {
  [[maybe_unused]] nothing!(); // expected-error {{an attribute list cannot appear here}}
}
