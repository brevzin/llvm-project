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

// Reflections of overload sets (P2996 ext):
// - ^^f, for f naming several functions and function templates, reflects the
//   overload set lookup found at that point;
// - splicing it performs overload resolution among exactly those candidates
//   (no argument-dependent lookup, no access checking);
// - is_overload_set, overloads_of, and make_overload_set inspect and build
//   sets. A single candidate is always reflected as itself, never as a set.

#include <meta>
#include <vector>

using std::meta::info;

namespace basic {
constexpr auto f(short) -> int { return 1; } // #1
constexpr auto f(long) -> int { return 2; }  // #2

constexpr auto r = ^^f; // the overload set of #1 and #2

constexpr auto f(int) -> int { return 3; }   // #3

static_assert(std::meta::is_overload_set(r));
static_assert(overloads_of(r).size() == 2);
static_assert(overloads_of(^^f).size() == 3);
static_assert(r != ^^f);

static_assert([: ^^f :](1) == 3);       // #3, which r does not have
static_assert([: r :](short(1)) == 1);
static_assert([: r :](1L) == 2);
// ([: r :](1) is ambiguous: see overload-sets.verify.cpp.)

// The candidates, in declaration order, are the functions themselves.
static_assert(type_of(parameters_of(overloads_of(r)[0])[0]) == ^^short);
static_assert(type_of(parameters_of(overloads_of(r)[1])[0]) == ^^long);
static_assert(std::meta::is_function(overloads_of(r)[0]));

// What the candidates agree on.
static_assert(identifier_of(r) == "f");
static_assert(has_identifier(r));
static_assert(parent_of(r) == ^^basic);
static_assert(source_location_of(r).line() ==
              source_location_of(overloads_of(r)[0]).line());
} // namespace basic

namespace single {
// One candidate is reflected as itself.
int g(int);
template <class T> int h(T);
static_assert(!std::meta::is_overload_set(^^g));
static_assert(overloads_of(^^g) == std::vector{^^g});
static_assert(overloads_of(^^h) == std::vector{^^h});
static_assert(std::meta::make_overload_set({^^g}) == ^^g);
static_assert(std::meta::make_overload_set({^^h, ^^h}) == ^^h);
static_assert(!std::meta::is_overload_set(^^int));
} // namespace single

namespace equality {
constexpr int a(int) { return 1; }
constexpr int b(double) { return 2; }

// Sets are equal when they have the same candidates: order and duplicates
// don't matter, and a set's candidates can be included in another.
constexpr info s = std::meta::make_overload_set({^^a, ^^b});
static_assert(std::meta::is_overload_set(s));
static_assert(s == std::meta::make_overload_set({^^b, ^^a, ^^a}));
static_assert(s == std::meta::make_overload_set({s, ^^a}));
static_assert(overloads_of(s) == std::vector{^^a, ^^b});

// Candidates from different names: overload resolution across them...
static_assert([: s :](1) == 1 && [: s :](1.0) == 2);
// ...but no name, and no identifier, to share.
static_assert(!has_identifier(s));
} // namespace equality

namespace templates {
template <class T> constexpr int t(T) { return 1; }
constexpr int t(int) { return 2; }

constexpr info s = ^^t;
static_assert(std::meta::is_overload_set(s));
static_assert([: s :](1) == 2 && [: s :](1.0) == 1);

// Explicit template arguments: only the template is viable.
static_assert(template [: s :]<int>(1) == 1);

// Taking the address resolves against the target type.
constexpr int (*p)(int) = [: s :];
constexpr int (*q)(double) = [: s :];
static_assert(p(0) == 2 && q(0) == 1);
} // namespace templates

namespace no_adl {
namespace N {
struct S {};
constexpr int call(S) { return 1; }
} // namespace N
constexpr int call(int) { return 2; }
constexpr int call(long) { return 3; }

// [: ^^call :](N::S{}) does not find N::call (see the verify test), but a
// set can include it explicitly.
constexpr info s = std::meta::make_overload_set({^^call, ^^N::call});
static_assert([: s :](N::S{}) == 1 && [: s :](1) == 2 && [: s :](1L) == 3);
} // namespace no_adl

namespace hidden {
// Hidden friends, which no qualified or unqualified lookup finds, can be
// called by name through a set.
struct C {
  friend constexpr int op(C, int) { return 1; }
  friend constexpr int op(C, double) { return 2; }
};
constexpr info s = std::meta::make_overload_set(hidden_friends_of(^^C));
static_assert([: s :](C{}, 1) == 1 && [: s :](C{}, 1.0) == 2);
} // namespace hidden

namespace members {
struct M {
  constexpr int m(int) const { return 1; }
  constexpr int m(double) const { return 2; }
  static constexpr int sm(int) { return 3; }
  static constexpr int sm(double) { return 4; }

  // Inside a member function, ^^m (an implicit this->m) is the set too.
  constexpr int via_this() const {
    constexpr info r = ^^m;
    return this->[: r :](1.0);
  }
};
constexpr info r = ^^M::m;
static_assert(std::meta::is_overload_set(r));
static_assert(M{}.[: r :](1) == 1 && M{}.[: r :](1.0) == 2);
static_assert([: ^^M::sm :](1) == 3 && [: ^^M::sm :](1.0) == 4);
static_assert(parent_of(r) == ^^M);
static_assert(M{}.via_this() == 2);

// Through a derived object, and through a pointer.
struct D : M {};
constexpr D d;
static_assert(D{}.[: r :](1) == 1 && (&d)->[: r :](1.0) == 2);

// A member brought in by a using-declaration joins the derived class's
// overloads; the set holds the functions themselves.
struct B {
  constexpr int f(int) const { return 1; }
};
struct E : B {
  using B::f;
  constexpr int f(double) const { return 2; }
};
static_assert(E{}.[: ^^E::f :](1) == 1 && E{}.[: ^^E::f :](1.0) == 2);
static_assert(std::meta::make_overload_set(overloads_of(^^E::f)) == ^^E::f);
} // namespace members

namespace using_decls {
namespace A {
constexpr int u(int) { return 1; }
constexpr int u(double) { return 2; }
} // namespace A
using A::u;
static_assert(std::meta::is_overload_set(^^u));
static_assert(^^u == ^^A::u);
static_assert([: ^^u :](1.0) == 2);
} // namespace using_decls

namespace operators {
struct V {
  int v;
};
constexpr bool operator==(V a, int b) { return a.v == b; }
constexpr bool operator==(V a, char const* s) { return s[0] - '0' == a.v; }
constexpr info eq = ^^operator==;
static_assert(std::meta::is_overload_set(eq));
static_assert([: eq :](V{1}, 1) && [: eq :](V{2}, "2"));
static_assert(!has_identifier(eq));
static_assert(operator_of(overloads_of(eq)[0]) ==
              std::meta::operators::op_equals_equals);
} // namespace operators

namespace in_templates {
// A non-dependent ^^k in a template is the set at the template's definition.
constexpr int k(int) { return 1; }
constexpr int k(double) { return 2; }
template <class T>
constexpr int call_k(T t) {
  return [: ^^k :](t);
}
constexpr int k(char) { return 3; } // not a candidate of call_k's set
static_assert(call_k(1) == 1 && call_k(1.0) == 2 && call_k('a') == 1);
} // namespace in_templates

int main(int, char**) { return 0; }
