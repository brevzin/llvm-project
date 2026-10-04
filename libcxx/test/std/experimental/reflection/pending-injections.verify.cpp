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

// Only a class template that may receive members per specialization defers
// lookup of names it does not have; the others still diagnose them when the
// template is defined. And a name the specialization does not receive is
// diagnosed at instantiation.

#include <meta>

__macro maybe(bool cond, std::meta::token_sequence decl) {
  if (extract<bool>(constant_of(cond)))
    return decl;
  return ^^{};
}

template <class T>
struct Plain {
  int f() { return this->missing; } // expected-error {{no member named 'missing' in 'Plain<T>'}}
  Plain() : missing(1) {} // expected-error {{member initializer 'missing' does not name a non-static data member or base class}}
};

// An annotation without an inject_members callback injects nothing.
struct inert {};
template <class T>
struct [[=inert{}]] Annotated {
  int f() { return this->missing; } // expected-error {{no member named 'missing' in 'Annotated<T>'}}
};

template <bool B>
struct C {
  maybe!(B, long sometimes);
  long f() { return this->sometimes; } // expected-error {{no member named 'sometimes' in 'C<false>'}}
};
long use = C<false>().f(); // expected-note {{in instantiation of member function 'C<false>::f' requested here}}

// A deferred mem-initializer whose member the specialization does not receive.
template <bool B>
struct M {
  maybe!(B, int sometimes);
  M() : sometimes(1) {} // expected-error {{member initializer 'sometimes' does not name a non-static data member or base class}}
};
M<true> ok;
M<false> bad; // expected-note {{in instantiation of member function 'M<false>::M' requested here}}
