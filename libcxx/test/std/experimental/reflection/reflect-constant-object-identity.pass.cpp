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
// ADDITIONAL_COMPILE_FLAGS: -std=c++2d -freflection

// P4340 ext: for a class with a user-provided reflect_constant, the object it
// returns *is* the template parameter object. Everything that produces "the
// template parameter object for v" -- reflect_constant, template_arguments_of,
// constant_of, an annotation's constant -- produces an object reflection of
// it (not a reflection of the variable that declares it), and they agree.

#include <meta>
#include <numeric>

struct Frac {
  int numer, denom;
  consteval auto reflect_constant() const -> std::meta::info;
};
template <int N, int D> constexpr auto interned = Frac{N, D};
consteval auto Frac::reflect_constant() const -> std::meta::info {
  int g = std::gcd(numer, denom);
  return substitute(^^interned, {std::meta::reflect_constant(numer / g),
                                 std::meta::reflect_constant(denom / g)});
}

template <Frac F> struct X {};
template <Frac F> constexpr int vt = 0;
template <Frac F> void ft() {}
template <auto V> struct Any {};

struct K { const char* p; };
struct Outer { Frac f; };
struct Plain { int x; };
constexpr K k{"hi"};
constexpr Outer o{{2, 4}};
constexpr Frac f24{2, 4};
constexpr const char* hi = "hi";
struct [[=Frac{2, 4}]] A {};
struct [[=Frac{1, 2}]] B {};

constexpr auto obj = std::meta::reflect_object(interned<1, 2>);

// ------------------------------------------------- template_arguments_of ----

static_assert(template_arguments_of(^^X<Frac{3, 6}>)[0] == obj);
static_assert(template_arguments_of(^^vt<Frac{2, 4}>)[0] == obj);
static_assert(template_arguments_of(^^ft<Frac{2, 4}>)[0] == obj);
static_assert(template_arguments_of(
    substitute(^^X, {std::meta::reflect_constant(Frac{1, 2})}))[0] == obj);

// ------------------------------------------------------ reflect_constant ----

static_assert(std::meta::reflect_constant(Frac{2, 4}) == obj);
static_assert(is_object(std::meta::reflect_constant(Frac{2, 4})));
static_assert(!is_variable(std::meta::reflect_constant(Frac{2, 4})));
static_assert(std::meta::reflect_constant(Frac{2, 4}) ==
              template_arguments_of(^^X<Frac{2, 4}>)[0]);

// The object reflection round-trips through splice and extract.
static_assert(&[:std::meta::reflect_constant(Frac{2, 4}):] == &interned<1, 2>);
static_assert(&std::meta::extract<const Frac&>(
                  std::meta::reflect_constant(Frac{2, 4})) == &interned<1, 2>);

// Subobjects are normalized (customized members, string literals).
static_assert(std::meta::reflect_constant(k) ==
              std::meta::reflect_constant(K{std::define_static_string("hi")}));
static_assert(std::meta::reflect_constant(o) ==
              std::meta::reflect_constant(Outer{{1, 2}}));

// A class without a customization still gets an interned template parameter
// object, and agrees with template_arguments_of.
static_assert(is_object(std::meta::reflect_constant(Plain{1})));
static_assert(std::meta::reflect_constant(Plain{1}) ==
              template_arguments_of(^^Any<Plain{1}>)[0]);

// ----------------------------------------------------------- constant_of ----

// Customized at the top level, or reached through its own variable.
static_assert(constant_of(^^f24) == obj);
static_assert(constant_of(^^f24) == std::meta::reflect_constant(f24));
static_assert(constant_of(^^interned<1, 2>) == obj);

// Customized and string-literal subobjects.
static_assert(constant_of(^^o) == std::meta::reflect_constant(o));
static_assert(constant_of(^^k) == std::meta::reflect_constant(k));

// A bare pointer into a string literal.
static_assert(constant_of(^^hi) == std::meta::reflect_constant(hi));

// A reflect_constant result is a fixed point.
static_assert(constant_of(std::meta::reflect_constant(Frac{2, 4})) ==
              std::meta::reflect_constant(Frac{2, 4}));
static_assert(constant_of(std::meta::reflect_constant(Plain{1})) ==
              std::meta::reflect_constant(Plain{1}));

// ----------------------------------------------------------- annotations ----

static_assert(constant_of(annotations_of(^^A)[0]) == obj);
// Equal after normalization: the same constant.
static_assert(constant_of(annotations_of(^^A)[0]) ==
              constant_of(annotations_of(^^B)[0]));

int main() {}
