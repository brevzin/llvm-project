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

// std::meta::operator_form_of is not a constant expression for anything but an
// operator function (or template) that implements an operator expression.

#include <cstddef>
#include <meta>

struct A {
  void* operator new(std::size_t);
  void operator delete(void*);
  void f();
};
template <class... Ts> int operator-(Ts...);
int operator""_x(unsigned long long);

constexpr auto a = operator_form_of(^^A::operator new);
// expected-error@-1 {{constexpr variable 'a' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of an operator function that implements an operator expression}}

constexpr auto b = operator_form_of(^^A::operator delete);
// expected-error@-1 {{constexpr variable 'b' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of an operator function that implements an operator expression}}

struct T {
  int x;
  consteval operator template() const = default;
};
consteval auto members_operator_template() -> std::meta::info {
  for (auto m : members_of(^^T, std::meta::access_context::unchecked()))
    if (is_operator_function(m) &&
        operator_of(m) == std::meta::operators::op_template)
      return m;
  return {};
}
constexpr auto t = operator_form_of(members_operator_template());
// expected-error@-1 {{constexpr variable 't' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of an operator function that implements an operator expression}}

constexpr auto c = operator_form_of(^^operator-);
// expected-error@-1 {{constexpr variable 'c' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of an operator function template without a function parameter pack}}

constexpr auto d = operator_form_of(^^A::f);
// expected-error@-1 {{constexpr variable 'd' must be initialized by a constant expression}}
// expected-note@*:* {{is not an operator or operator template}}

constexpr auto e = operator_form_of(^^operator""_x);
// expected-error@-1 {{constexpr variable 'e' must be initialized by a constant expression}}
// expected-note@*:* {{is not an operator or operator template}}

constexpr auto f = operator_form_of(^^A);
// expected-error@-1 {{constexpr variable 'f' must be initialized by a constant expression}}
// expected-note@*:* {{is not an operator or operator template}}
