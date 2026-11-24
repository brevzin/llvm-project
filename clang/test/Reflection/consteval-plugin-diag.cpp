// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// REQUIRES: plugins
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clangxx -std=c++26 -freflection -fconsteval-plugin -fPIC -shared -O2 \
// RUN:   %t/plugin.cpp -o %t/libplugin.so
// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-plugin-load=%t/libplugin.so \
// RUN:   -fsyntax-only -verify %t/consumer.cpp

//--- plugin.cpp
consteval auto check_positive(int x) -> int {
  if (x == 0) {
    __builtin_constexpr_diag(0, "", 0, "zero value", 10);
    __builtin_constexpr_diag(1, "warn-tag", 8, "prefer positive", 15);
    return 0;
  }
  if (x < 0) {
    __builtin_constexpr_diag(2, "range", 5, "x must be positive", 18);
    // Must not reach here: error aborts plugin invocation immediately.
    *(volatile int *)0 = 0;
  }
  return x * 2;
}

//--- consumer.cpp
consteval auto check_positive(int x) -> int;

static_assert(check_positive(5) == 10);
constexpr int z = check_positive(0);    // expected-note {{constexpr message: zero value}}
                                        // expected-warning@-1 {{constexpr message with tag 'warn-tag': prefer positive}}
constexpr int bad = check_positive(-1); // expected-error {{constexpr variable 'bad' must be initialized by a constant expression}}
                                        // expected-note@-1 {{constexpr message with tag 'range': x must be positive}}
