// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A plugin that cannot be used says so: one that is not there, is not a
// plugin, was built by another compiler, or was built against declarations
// that are not the ones it is used with. A plugin can have more than one
// translation unit.
//
// REQUIRES: plugins
//
// RUN: rm -rf %t && split-file %s %t
//
// RUN: not %clang_cc1 -std=c++26 -freflection -fsyntax-only %t/consumer.cpp \
// RUN:   -fconsteval-plugin-load=%t/libmissing.so 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING
// MISSING: error: unable to load consteval plugin '{{.*}}libmissing.so': {{.*}}libmissing.so
//
// RUN: %clangxx -std=c++26 -fPIC -shared %t/notplugin.cpp -o %t/libnotplugin.so
// RUN: not %clang_cc1 -std=c++26 -freflection -fsyntax-only %t/consumer.cpp \
// RUN:   -fconsteval-plugin-load=%t/libnotplugin.so 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOTPLUGIN
// NOTPLUGIN: error: unable to load consteval plugin '{{.*}}libnotplugin.so': it was not built with -fconsteval-plugin
//
// RUN: %clangxx -std=c++26 -fPIC -shared %t/otherabi.cpp -o %t/libotherabi.so
// RUN: not %clang_cc1 -std=c++26 -freflection -fsyntax-only %t/consumer.cpp \
// RUN:   -fconsteval-plugin-load=%t/libotherabi.so 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OTHERABI
// OTHERABI: error: unable to load consteval plugin '{{.*}}libotherabi.so': it was built by a different compiler ('some other compiler'; this is '{{.*}}')
//
// Two translation units, one plugin:
// RUN: %clangxx -std=c++26 -freflection -fconsteval-plugin -fPIC -shared -O2 \
// RUN:   %t/one.cpp %t/two.cpp -o %t/libplugin.so
// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-plugin-load=%t/libplugin.so \
// RUN:   -fsyntax-only -verify %t/consumer.cpp
//
// ... and loaded with another, the first that defines a function is used:
// RUN: %clangxx -std=c++26 -freflection -fconsteval-plugin -fPIC -shared -O2 \
// RUN:   %t/three.cpp -o %t/libthree.so
// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only -verify -DTHREE \
// RUN:   -fconsteval-plugin-load=%t/libplugin.so \
// RUN:   -fconsteval-plugin-load=%t/libthree.so %t/consumer.cpp
//
// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-plugin-load=%t/libplugin.so \
// RUN:   -fsyntax-only -verify %t/mismatch.cpp
//
// RUN: not %clang_cc1 -std=c++26 -freflection -fconsteval-operations \
// RUN:   -fconsteval-plugin-load=%t/libplugin.so -fsyntax-only %t/consumer.cpp 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OPERATIONS
// OPERATIONS: error: unable to load consteval plugin '{{.*}}libplugin.so': plugins cannot be used with -fconsteval-operations

//--- notplugin.cpp
int not_a_plugin() { return 0; }

//--- otherabi.cpp
extern "C" const char __clang_consteval_plugin_abi[] = "some other compiler";
extern "C" void __clang_consteval_init(const void *) {}

//--- shared.h
struct Pair {
  int a;
  int b;
};
// (Defined in a header, so in both translation units of the plugin.)
consteval auto twice(int x) -> int { return 2 * x; }

//--- one.cpp
#include "shared.h"
consteval auto sum(Pair p) -> int { return twice(p.a) + p.b; }

//--- two.cpp
#include "shared.h"
using info = decltype(^^int);
consteval auto is_int(info r) -> bool { return r == ^^int; }
consteval auto product(Pair p) -> int { return twice(p.a) * p.b; }

//--- three.cpp
consteval auto difference(int a, int b) -> int { return a - b; }
// (Not the one used: libplugin.so comes first.)
consteval auto twice(int x) -> int { return 3 * x; }

//--- consumer.cpp
// expected-no-diagnostics
struct Pair {
  int a;
  int b;
};
using info = decltype(^^int);
consteval auto sum(Pair p) -> int;
consteval auto product(Pair p) -> int;
consteval auto is_int(info r) -> bool;
consteval auto twice(int x) -> int;
static_assert(sum(Pair{3, 4}) == 10);
static_assert(product(Pair{3, 4}) == 24);
static_assert(is_int(^^int) && !is_int(^^long));
static_assert(twice(21) == 42);
#ifdef THREE
consteval auto difference(int a, int b) -> int;
static_assert(difference(5, 3) == 2);
#endif

//--- mismatch.cpp
// The plugin was built with a Pair of two ints.
struct Pair {
  long a;
  int b;
};
consteval auto sum(Pair p) -> int;
consteval auto twice(long x) -> int; // (Another function: the plugin has none.)
constexpr int x = sum(Pair{1, 2}); // expected-error {{must be initialized by a constant expression}} \
  // expected-note {{call to 'sum' in a consteval plugin failed: its parameter and return types are not laid out here as they were where the plugin was built (here '({16:0=i64s;64=i32s;},)->i32s', in the plugin '({8:0=i32s;32=i32s;},)->i32s')}}
constexpr int y = twice(1L); // expected-error {{must be initialized by a constant expression}} \
  // expected-note {{undefined function 'twice' cannot be used in a constant expression}}
// expected-note@-5 {{declared here}}
