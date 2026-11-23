//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -verify
// RUN: %clang_cc1 %s -std=c++2d -freflection -verify

// An annotation of non-structural type says why the type isn't structural,
// as a constant template parameter of that type would. (A struct holding a
// std::string is the common case: before C++29, std::string isn't structural,
// and its value often isn't a constant template argument either, since it
// refers to an allocation. The type is the more useful thing to point at.)

struct Hidden {
  constexpr Hidden(int i) : i(i) {}
private:
  int i; // expected-note 3{{'Hidden' is not a structural type because it has a non-static data member that is not public}}
};

struct Outer {
  int x;
  Hidden h; // expected-note {{'Outer' is not a structural type because it has a non-static data member of non-structural type 'Hidden'}}
};

struct Owner {
  int *p;
  constexpr Owner() : p(new int(1)) {}
  constexpr ~Owner() { delete p; }
private:
  int pad = 0; // expected-note {{'Owner' is not a structural type because it has a non-static data member that is not public}}
};

[[=Hidden(1)]] int a; // expected-error {{C++26 annotation attribute requires a value of structural type}}
[[=Outer{1, 2}]] int b; // expected-error {{C++26 annotation attribute requires a value of structural type}}

// The type is checked before the value: no mention of the allocation.
[[=Owner()]] int c; // expected-error {{C++26 annotation attribute requires a value of structural type}}

template <class T>
struct Box {
  // Dependent: checked on instantiation.
  [[=T(1)]] int d; // expected-error {{C++26 annotation attribute requires a value of structural type}}
};
Box<Hidden> box; // expected-note {{in instantiation of template class 'Box<Hidden>' requested here}}
