//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -std=c++2d -ast-print %s | FileCheck %s --check-prefix=PRINT
// RUN: %clang_cc1 -std=c++2d -ast-dump %s | FileCheck %s --check-prefix=DUMP

// P2481 bounded template type parameters, printed and dumped.

struct B {};

// PRINT: template <typename T : B, class ...Ts -> int, typename U : B = B> void f(T &&, Ts ...);
// DUMP:      FunctionTemplateDecl {{.*}} f
// DUMP-NEXT: TemplateTypeParmDecl {{.*}} <col:11, col:24> col:20 referenced typename depth 0 index 0 T : 'B'
// DUMP-NEXT: TemplateTypeParmDecl {{.*}} <col:27, col:42> col:36 referenced class depth 0 index 1 ... Ts -> 'int'
// DUMP-NEXT: TemplateTypeParmDecl {{.*}} <col:47, col:64> col:56 typename depth 0 index 2 U : 'B'
template <typename T : B, class... Ts -> int, typename U : B = B> void f(T&&, Ts...);

template <class X> struct S {
  // PRINT: template <typename T : X> void g(T &&);
  template <typename T : X> void g(T&&);
};

// The bound is instantiated with the class.
// DUMP:      ClassTemplateSpecializationDecl {{.*}} struct S definition
// DUMP:      FunctionTemplateDecl {{.*}} g
// DUMP-NEXT: TemplateTypeParmDecl {{.*}} typename depth 0 index 0 T : 'B'
template struct S<B>;
