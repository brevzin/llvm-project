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
// ADDITIONAL_COMPILE_FLAGS: -foperator-for -std=c++2d

// -foperator-for: 'operator for' has no return type, and 'co_yield' in it
// initializes the loop variable from one expression.

struct with_type {
  void operator for() const {} // expected-error {{'operator for' cannot have a return type}}
};

struct yields_list {
  operator for() const {
    co_yield {1, 2}; // expected-error {{the loop variable cannot be initialized from an initializer list here}}
  }
};

struct never_yields {
  operator for() const {}
};

void f() {
  for (auto x : never_yields{}) // expected-error {{cannot deduce the type of 'x': 'operator for' of 'never_yields' never inserts the loop body}}
    (void)x;
  for (int x : never_yields{}) // fine: the body is never inserted
    (void)x;
}
