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
// RUN:   -fsyntax-only -Wundefined-inline -verify %t/consumer.cpp

//--- plugin.cpp
#include "token-sequence.h"
using info = decltype(^^int);

namespace std::meta {
consteval auto queue_injection(token_sequence tokens) -> void;
}

struct FieldCfg {
  info type;
  int default_val;
};

consteval auto inject_field(FieldCfg cfg) -> void {
  std::meta::queue_injection(^^{
    \(cfg.type) value = \(cfg.default_val);
    constexpr auto get() const -> \(cfg.type) { return value; }
  });
}

consteval auto is_int_type(info t) -> bool {
  return t == ^^int;
}

//--- consumer.cpp
// expected-no-diagnostics
// Zero #include <meta> in consumer!
using info = decltype(^^int);

struct FieldCfg {
  info type;
  int default_val;
};

consteval auto inject_field(FieldCfg cfg) -> void;
consteval auto is_int_type(info t) -> bool;

static_assert(is_int_type(^^int));
static_assert(!is_int_type(^^double));

struct Box {
  consteval { inject_field(FieldCfg{^^int, 42}); }
};

static_assert(Box{}.get() == 42);
static_assert(Box{7}.get() == 7);
