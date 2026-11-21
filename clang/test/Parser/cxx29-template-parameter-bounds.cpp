//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -std=c++2d -fsyntax-only -verify %s

// P2481 bounded template type parameters: 'typename T : type-id' and
// 'typename T -> type-id', after the name and before any default argument.

struct Base {};
template <class> struct X {};
template <class T> concept C = true;

template <typename T : Base> void a1();
template <class T : Base> void a2();
template <typename... Ts : Base> void a3();
template <class... Ts -> int> void a4();
template <C T : Base> void a5();
template <C... Ts -> Base> void a6();
template <typename T : Base = Base> void a7();
template <typename T -> X<int>> void a8();
template <typename T -> X<X<int>>> void a9();
template <typename T -> Base, typename U = T> void a10();
template <typename : Base> void a11();
template <typename -> int> void a12();
template <typename T : ::Base> void a13();
template <typename T, typename T::type N> void a14(); // still a non-type parameter
auto l = []<typename T : Base, typename U -> int>(T, U) {};

template <typename T : > void b1(); // expected-error {{expected a type}}
// ':::' lexes as '::' ':', as '<::' does: a space is needed.
template <typename T:::Base> void b2(); // expected-error {{use of undeclared identifier 'T'}} \
                                        // expected-error {{expected an identifier or template-id after '::'}} \
                                        // expected-error {{expected ',' or '>' in template-parameter-list}}
