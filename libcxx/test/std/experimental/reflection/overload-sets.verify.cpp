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

// Reflections of overload sets: what they cannot do.

#include <meta>
#include <vector>

using std::meta::info;

// Overload resolution is among the captured candidates only: #3 is not one.
constexpr auto f(short) -> int { return 1; } // #1 expected-note {{candidate function}}
constexpr auto f(long) -> int { return 2; }  // #2 expected-note {{candidate function}}
constexpr auto r = ^^f;
constexpr auto f(int) -> int { return 3; }   // #3
int i = [: r :](1); // expected-error {{call to 'f' is ambiguous}}

// No argument-dependent lookup.
namespace N {
struct S {};
constexpr int call(S) { return 1; }
} // namespace N
constexpr int call(int) { return 2; }  // expected-note {{candidate function not viable}}
constexpr int call(long) { return 3; } // expected-note {{candidate function not viable}}
int j = [: ^^call :](N::S{}); // expected-error {{no matching function for call to 'call'}}

// make_overload_set: at least one candidate...
constexpr info empty = std::meta::make_overload_set(std::vector<info>{});
// expected-error@-1 {{constexpr variable 'empty' must be initialized by a constant expression}}
// expected-note@-2 {{in call to}}
// expected-note@-3 {{an overload set needs at least one function or function template}}

// ...each a function, function template, or overload set...
constexpr info bad = std::meta::make_overload_set({^^int});
// expected-error@-1 {{constexpr variable 'bad' must be initialized by a constant expression}}
// expected-note@-2 {{in call to}}
// expected-note@-3 {{expected a reflection of a function, function template, or overload set, but got a type}}

// ...and with a non-static member function, only members of its class (or
// its bases): member call syntax needs a class for the object.
struct M {
  int m(int);
  int m(double);
};
constexpr info mixed = std::meta::make_overload_set({^^M::m, ^^call});
// expected-error@-1 {{constexpr variable 'mixed' must be initialized by a constant expression}}
// expected-note@-2 {{in call to}}
// expected-note@-3 {{cannot form an overload set of 'm', a non-static member function, and 'call', which is not a member of the same class or its bases}}

// An overload set has no type (or value, or object).
constexpr info t = type_of(^^call);
// expected-error@-1 {{constexpr variable 't' must be initialized by a constant expression}}
// expected-note@-2 {{in call to}}
// expected-note@-3 {{an overload set has no type}}

// As with one member function: no implicit 'this'.
int k = [: ^^M::m :](1);
// expected-error@-1 {{cannot implicitly reference a class member through a splice}}
// expected-note@-2 {{an explicit 'this' pointer may fix the problem}}

// Member call syntax needs members.
int l = M{}.[: ^^call :](1);
// expected-error@-1 {{use of splice as qualified member access requires the reflection to be a member of a class}}

// Template arguments need a function template among the candidates.
int n = template [: ^^call :]<int>(1);
// expected-error@-1 {{reflection not usable in a template splice}}
