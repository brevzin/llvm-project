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
// RUN:   -fsyntax-only -Wundefined-inline -verify %t/consumer.cpp
// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-plugin-load=%t/libplugin.so \
// RUN:   -fsyntax-only -ftime-trace=%t/trace.json -ftime-trace-granularity=0 %t/consumer.cpp
// RUN: %python -c 'import json, sys; [print(e["name"], e.get("args", {}).get("detail", "")) for e in json.load(open(sys.argv[1]))["traceEvents"] if e.get("ph") == "X"]' %t/trace.json \
// RUN:   | FileCheck %s
//
// CHECK-DAG: {{^}}ConstevalPlugin sum_up_to{{$}}
// CHECK-DAG: {{^}}ConstevalPlugin weighted_sum{{$}}

//--- plugin.cpp
struct Pair {
  int a;
  int b;
};

consteval auto sum_up_to(int n) -> long long {
  long long s = 0;
  for (int i = 1; i <= n; ++i)
    s += i;
  return s;
}

consteval auto weighted_sum(Pair p, int scale) -> int {
  return (p.a + p.b) * scale;
}

//--- consumer.cpp
// expected-no-diagnostics
struct Pair {
  int a;
  int b;
};

consteval auto sum_up_to(int n) -> long long;
consteval auto weighted_sum(Pair p, int scale) -> int;

static_assert(sum_up_to(100) == 5050);
static_assert(weighted_sum(Pair{3, 4}, 6) == 42);
