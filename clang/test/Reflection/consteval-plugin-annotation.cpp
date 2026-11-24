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
using info = decltype(^^int);

struct AddTag {
  int value;
  consteval auto inject_members(info) const -> std::meta::token_sequence {
    int v = value;
    return ^^{
      static constexpr int tag = \(v);
    };
  }
};

//--- consumer.cpp
// expected-no-diagnostics
#include "token-sequence.h"
using info = decltype(^^int);

struct AddTag {
  int value;
  consteval auto inject_members(info) const -> std::meta::token_sequence;
};

struct [[=AddTag{42}]] S {
  int x;
};

static_assert(S::tag == 42);
