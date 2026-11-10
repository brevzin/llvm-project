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

// std::meta::object_parameter_of is not a constant expression for anything but
// a non-static member function other than a constructor or destructor, and an
// implicit object parameter has neither an identifier nor a variable.

#include <meta>

struct S {
  S();
  ~S();
  static void sf();
  void f();
  template <class T> void tf();
};
void g();

constexpr auto a = object_parameter_of(^^S::sf);
// expected-error@-1 {{constexpr variable 'a' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a non-static member function other than a constructor or destructor}}

consteval auto ctor() -> std::meta::info {
  for (auto m : members_of(^^S, std::meta::access_context::unchecked()))
    if (is_constructor(m))
      return m;
  return {};
}
constexpr auto b = object_parameter_of(ctor());
// expected-error@-1 {{constexpr variable 'b' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a non-static member function other than a constructor or destructor}}

constexpr auto c = object_parameter_of(^^S::~S);
// expected-error@-1 {{constexpr variable 'c' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a non-static member function other than a constructor or destructor}}

constexpr auto d = object_parameter_of(^^g);
// expected-error@-1 {{constexpr variable 'd' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a non-static member function other than a constructor or destructor}}

constexpr auto e = object_parameter_of(^^S::tf);
// expected-error@-1 {{constexpr variable 'e' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a non-static member function other than a constructor or destructor}}

constexpr auto f = object_parameter_of(^^S);
// expected-error@-1 {{constexpr variable 'f' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a non-static member function other than a constructor or destructor}}

constexpr auto h = object_parameter_of(object_parameter_of(^^S::f));
// expected-error@-1 {{constexpr variable 'h' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a non-static member function other than a constructor or destructor}}

constexpr auto i = identifier_of(object_parameter_of(^^S::f));
// expected-error@-1 {{constexpr variable 'i' must be initialized by a constant expression}}
// expected-note@*:* {{is anonymous and has no associated identifier}}

void S::f() {
  constexpr auto j = variable_of(object_parameter_of(^^S::f));
  // expected-error@-1 {{constexpr variable 'j' must be initialized by a constant expression}}
  // expected-note@*:* {{expected a reflection of a function parameter other than an implicit object parameter}}
}
