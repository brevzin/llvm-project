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

// P4340 ext: 'operator template' can be named only in its own declaration.
// A program can't call it, take its address, reflect it with ^^, name it in a
// using-declaration, or find it with lookup -- nor splice or extract the
// reflection members_of gives of it. The language invokes it (for a constant
// template argument), and so does std::meta::reflect_constant.

#include <meta>

template <int N> struct Box { int n = N; };
template <int N> constexpr auto boxed = Box<N>{};

struct C {
  int n;
  consteval operator template() const;  // naming it in its declaration: fine
};
consteval C::operator template() const { return ^^boxed<1>; }  // and here
struct D {
  int n;
  consteval operator template() const = default;
};

consteval auto operator_template_of(std::meta::info cls) -> std::meta::info {
  for (auto m : members_of(cls, std::meta::access_context::unchecked()))
    if (is_operator_function(m) &&
        operator_of(m) == std::meta::operators::op_template)
      return m;
  return {};
}

// Fine: what the language does, std::meta::reflect_constant, members_of.
template <D d> constexpr int ok = d.n;
static_assert(ok<D{3}> == 3);
constexpr auto r = std::meta::reflect_constant(D{3});
static_assert(is_special_member_function(operator_template_of(^^C)));

// ------------------------------------------------------------------ calls ----

consteval auto call() {
  return D{1}.operator template();
  // expected-error@-1 {{'operator template' can be named only in its own declaration (use 'std::meta::reflect_constant' to invoke it)}}
}

consteval auto qualified_call() {
  D d{1};
  return d.D::operator template();
  // expected-error@-1 {{'operator template' can be named only in its own declaration}}
}

constexpr auto address = &C::operator template;
// expected-error@-1 {{'operator template' can be named only in its own declaration}}

template <class T>
consteval auto call_dependent(T t) {
  return t.operator template();
  // expected-error@-1 {{'operator template' can be named only in its own declaration}}
}
constexpr auto dependent = call_dependent(C{1});
// expected-note@-1 {{in instantiation of function template specialization 'call_dependent<C>' requested here}}

// ------------------------------------------------------------- reflection ----

constexpr auto reflected = ^^C::operator template;
// expected-error@-1 {{'operator template' can be named only in its own declaration}}

template <class T> constexpr auto reflected_dependent = ^^T::operator template;
// expected-error@-1 {{'operator template' can be named only in its own declaration}}

static_assert(!std::meta::lookup(^^C, std::meta::operators::op_template,
                                 std::meta::access_context::unchecked())
                   .has_value());

consteval auto spliced() {
  return C{1}.[:operator_template_of(^^C):]();
  // expected-error@-1 {{'operator template' can be named only in its own declaration}}
}

constexpr auto extracted = std::meta::extract<std::meta::info (C::*)() const>(
    operator_template_of(^^C));
// expected-error@-2 {{constexpr variable 'extracted' must be initialized by a constant expression}}
// expected-note@*:* {{cannot extract a pointer to member from a reflection of an operator function}}

// -------------------------------------------------------- using-declarations ----

struct E : D {
  using D::operator template;
  // expected-error@-1 {{'operator template' can be named only in its own declaration}}
};

template <class T> struct F : T {
  using T::operator template;
  // expected-error@-1 {{'operator template' can be named only in its own declaration}}
};

// ------------------------------------------------- an implicit one, likewise ----

struct G {
  int n;
};

consteval auto spliced_implicit() {
  return G{1}.[:operator_template_of(^^G):]();
  // expected-error@-1 {{'operator template' can be named only in its own declaration}}
}
