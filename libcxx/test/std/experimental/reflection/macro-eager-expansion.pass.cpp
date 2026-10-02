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
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// RUN: %{build}
// RUN: %{exec} %t.exe

// In a template, a macro invocation whose arguments are only value-dependent
// (their types are known) is expanded where it appears, as a non-dependent
// construct of the template would be. Its expansion waits for instantiation
// only if the body asks what such an argument evaluates to (or calls
// std::meta::expand_at_instantiation()). A type-dependent argument still
// defers it: the macro cannot be selected, nor its parameters bound, without
// the type.

#include <meta>
#include <cassert>
#include <string_view>

// ------------------------------------- names introduced are visible later --

__macro let(std::meta::token_sequence name, int init) {
  return ^^{ auto \(name) = \(init) };
}

template <int N>
int doubled_plus_one() {
  let!(x, N * 2);  // expanded in the template: 'x' is a local of the pattern
  return x + 1;
}

// ------------------------------------------ evaluated, then waits for it --

// Asks for the value: waits for instantiation without being told to.
__macro value_of(int v) {
  return ^^{ \(extract<int>(constant_of(v))) };
}

template <int N>
int value_plus(int k) {
  return value_of!(N + 1) + k;
}

// ------------------------------------------------ eager or deferred? -------

// Expanded eagerly, the macro sees the template itself as its expansion
// context; deferred, it sees the specialization.
__macro where(int v) {
  return has_template_arguments(std::meta::macro_expansion_context())
             ? ^^{ ((void)\(v), 2) }
             : ^^{ ((void)\(v), 1) };
}

__macro where_after_asking(int v) {
  (void)constant_of(v);
  return has_template_arguments(std::meta::macro_expansion_context())
             ? ^^{ ((void)\(v), 2) }
             : ^^{ ((void)\(v), 1) };
}

template <int N>
int eager() {
  return where!(N);
}

template <int N>
int deferred() {
  return where_after_asking!(N);
}

// Structural questions about the argument are answered in the template.
__macro spelled(int v) {
  (void)type_of(v);
  (void)is_binary_operation(v);
  return ^^{ \(std::meta::str_lit(source_text_of(v))) };
}

template <int N>
std::string_view spelling() {
  return spelled!(N + 1);
}

// ---------------------------------------------- type-dependent arguments --

template <class T>
__macro twice(T&& t) {
  return ^^{ (\(t) * 2) };
}

template <class T>
T doubled(T t) {
  return twice!(t);  // the macro is selected per instantiation
}

// -------------------------------------------------- statement position -----

__macro add_to(int& x, int y) {
  return ^^{ \(x) += \(y) };
}

// Arguments that are not dependent at all were always expanded in the
// template; a local they name is each instantiation's own.
template <class T>
int locals() {
  int a = 1;
  add_to!(a, 2);
  return a;
}

template <int N>
int accumulate() {
  int a = 1;
  add_to!(a, N);  // eager: parsed among the template's statements
  let!(b, a * 10);
  return b;
}

int main(int, char**) {
  assert(doubled_plus_one<3>() == 7);
  assert(value_plus<4>(10) == 15);
  assert(eager<5>() == 1);
  assert(deferred<5>() == 2);
  assert(spelling<0>() == "N + 1");
  assert(doubled(21) == 42);
  assert(doubled(1.5) == 3.0);
  assert(accumulate<2>() == 30);
  assert(locals<int>() == 3);
  return 0;
}
