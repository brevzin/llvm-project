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

// A using-declarator at namespace scope is transparent to the reflection
// operator (P4466, generalized to any namespace): the reflection represents the
// declaration that the using-declarator names. Reflecting a class-scope or
// block-scope using-declarator is still ill-formed; the error points at the
// name, qualified or not.

namespace ns {
enum class E { a };
enum Plain { p };
struct S {};
template <class T> struct Tmpl {};
template <class T> void ft(T) {}
template <class T> int vt = 0;
template <class T> concept C = true;
void f();
int v;
using Alias = int;

inline namespace v1 { struct InInline {}; }
using v1::InInline;
} // namespace ns

// A using-declarator naming a declaration in another namespace.
using ns::E;
static_assert(^^E == ^^ns::E);
static_assert(^^::E == ^^ns::E);

namespace other {
using ns::S;
using ns::Tmpl;
using ns::ft;
using ns::vt;
using ns::C;
using ns::f;
using ns::v;
using ns::Alias;
using ns::p;
} // namespace other

static_assert(^^other::S == ^^ns::S);
static_assert(^^other::Tmpl == ^^ns::Tmpl);
static_assert(^^other::Tmpl<int> == ^^ns::Tmpl<int>);
static_assert(^^other::ft == ^^ns::ft);
static_assert(^^other::ft<int> == ^^ns::ft<int>);
static_assert(^^other::vt == ^^ns::vt);
static_assert(^^other::vt<int> == ^^ns::vt<int>);
static_assert(^^other::C == ^^ns::C);
static_assert(^^other::f == ^^ns::f);
static_assert(^^other::v == ^^ns::v);
static_assert(^^other::Alias == ^^ns::Alias);
static_assert(^^other::Alias != ^^int);
static_assert(^^other::p == ^^ns::p);

// A using-declarator naming a declaration in its own namespace, or one
// separated from it only by an inline namespace (the case P4466 allows).
static_assert(^^ns::InInline == ^^ns::v1::InInline);

// A using-declarator naming another using-declarator.
namespace chain {
using other::f;
using other::S;
} // namespace chain
static_assert(^^chain::f == ^^ns::f);
static_assert(^^chain::S == ^^ns::S);

// The target scope is a namespace even if the using-declarator appears
// inside a linkage specification.
extern "C++" {
namespace linkage {
using ns::f;
}
}
static_assert(^^linkage::f == ^^ns::f);

// A using-enum-declaration at namespace scope.
namespace en {
using enum ns::Plain;
}
static_assert(^^en::p == ^^ns::p);

// The global namespace, reached by the motivating example
// 'namespace std { using ::int8_t; }'.
using int8 = signed char;
namespace std2 {
using ::int8;
}
static_assert(^^std2::int8 == ^^::int8);
static_assert(^^std2::int8 != ^^signed char);

// From within a template, and after substituting a spliced namespace.
template <class T> consteval auto in_template() { return ^^other::f; }
static_assert(in_template<int>() == ^^ns::f);

template <decltype(^^int) R> constexpr auto splice_f = ^^[:R:]::f;
static_assert(splice_f<^^other> == ^^ns::f);
template <decltype(^^int) R> constexpr auto splice_S = ^^typename [:R:]::S;
static_assert(splice_S<^^chain> == ^^ns::S);
template <decltype(^^int) R> constexpr auto splice_ft = ^^[:R:]::template ft<int>;
static_assert(splice_ft<^^other> == ^^ns::ft<int>);
template <decltype(^^int) R> constexpr auto splice_Tmpl = ^^[:R:]::template Tmpl;
static_assert(splice_Tmpl<^^other> == ^^ns::Tmpl);

// Class-scope using-declarators are opaque.
struct B {
  void m();
  struct Inner {};
  using Alias = int;
  int d;
  static int sd;
};
struct D : B {
  using B::m;
  using B::Inner;
  using B::Alias;
  using B::d;
  using B::sd;

  void g() {
    (void)^^m; // expected-error {{cannot take the reflection of a using-declarator}}
    (void)^^d; // expected-error {{cannot take the reflection of a using-declarator}}
  }
};
constexpr auto c1 = ^^D::m;     // expected-error {{cannot take the reflection of a using-declarator}}
constexpr auto c2 = ^^D::Inner; // expected-error {{cannot take the reflection of a using-declarator}}
constexpr auto c3 = ^^D::Alias; // expected-error {{cannot take the reflection of a using-declarator}}
constexpr auto c4 = ^^D::d;     // expected-error {{cannot take the reflection of a using-declarator}}
constexpr auto c5 = ^^D::sd;    // expected-error {{cannot take the reflection of a using-declarator}}

struct UsingEnum {
  using enum ns::E;
};
constexpr auto c6 = ^^UsingEnum::a; // expected-error {{cannot take the reflection of a using-declarator}}

template <class T>
constexpr auto dependent = ^^T::sd; // expected-error {{cannot take the reflection of a using-declarator}}
constexpr auto c7 = dependent<D>; // expected-note {{in instantiation of variable template specialization 'dependent' requested here}}

// Block-scope using-declarators are opaque, too.
void block() {
  using ns::S;
  using ns::f;
  using ns::v;
  using enum ns::E;
  (void)^^S; // expected-error {{cannot take the reflection of a using-declarator}}
  (void)^^f; // expected-error {{cannot take the reflection of a using-declarator}}
  (void)^^v; // expected-error {{cannot take the reflection of a using-declarator}}
  (void)^^a; // expected-error {{cannot take the reflection of a using-declarator}}
}
