//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -std=c++2d -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// P2481: a bounded parameter is deduced as the bound, so arguments of
// different types share a specialization, and the conversion (derived to
// base, or any conversion for '->') happens at the call.

struct Base { int v; };
struct Derived : Base { int w; };
struct Str { Str(const char *); };

template <typename T : Base> int f(T &&t) { return t.v; }
template <typename T -> Str> void g(T &&) {}
template <class T, class... Ts : T> int sum(Ts... xs) { return (xs + ... + 0); }

// CHECK-LABEL: define {{.*}} @_Z4testR4BaseR7Derived(
int test(Base &b, Derived &d) {
  // CHECK: call {{.*}} @_Z1fIR4BaseEiOT_(ptr {{.*}} %{{.*}})
  // CHECK: call {{.*}} @_Z1fIR4BaseEiOT_(ptr {{.*}} %{{.*}})
  // CHECK: call {{.*}} @_Z1fI4BaseEiOT_(
  int r = f(b) + f(d) + f(Derived{});
  // CHECK: call void @_ZN3StrC1EPKc(
  // CHECK: call void @_Z1gI3StrEvOT_(
  g("hello");
  // CHECK: call {{.*}} @_Z3sumIiJiiiEEiDpT0_(i32 {{.*}} 1, i32 {{.*}} 2, i32 {{.*}} 3)
  return r + sum(1, 2, 3);
}

// CHECK-NOT: define {{.*}} @_Z1fIR7Derived
// CHECK-NOT: define {{.*}} @_Z1gIRA6_KcEvOT_
