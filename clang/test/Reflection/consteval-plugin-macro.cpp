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
// RUN:   -I%S/Inputs %t/plugin.cpp -o %t/libplugin.so
// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-plugin-load=%t/libplugin.so \
// RUN:   -I%S/Inputs -fsyntax-only -Wundefined-inline -verify %t/consumer.cpp

//--- plugin.cpp
#include "token-sequence.h"
using std::meta::token_sequence;

__macro make_point(token_sequence name) {
  auto getter = std::meta::id("sum_", 1);
  return ^^{
    struct \(name) {
      int x;
      int y;
      constexpr auto \(getter)() const -> int { return x + y; }
    };
  };
}

//--- consumer.cpp
// expected-no-diagnostics
#include "token-sequence.h"

// Undefined __macro declaration resolved from native plugin!
__macro make_point(std::meta::token_sequence name);

make_point!(Pt);
static_assert(Pt{10, 32}.sum_1() == 42);
