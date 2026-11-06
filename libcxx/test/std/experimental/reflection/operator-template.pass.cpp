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

// P4340 ext: 'operator template', the special member function that says what
// a value of its class is as a constant template argument. Like a conversion
// function it has no return type of its own -- its name says it returns
// std::meta::info -- and, being an operator function, its name can't collide
// with anything a class (or an unqualified call) might already use. It can be
// named only in its own declaration (see operator-template.verify.cpp), but
// members_of finds it.

#include <meta>
#include <numeric>

using std::meta::access_context;
using std::meta::info;
using std::meta::operators;

struct Frac {
  int numer, denom;
  consteval operator template() const;  // no return type
};
template <int N, int D> constexpr auto interned = Frac{N, D};
consteval Frac::operator template() const {  // defined out of line
  int g = std::gcd(numer, denom);
  return substitute(^^interned, {std::meta::reflect_constant(numer / g),
                                 std::meta::reflect_constant(denom / g)});
}

// It is what a value of the class is as a template argument.
template <Frac F> constexpr const Frac* addr = &F;
static_assert(addr<Frac{2, 4}> == &interned<1, 2>);
static_assert(std::meta::reflect_constant(Frac{2, 4}) ==
              std::meta::reflect_object(interned<1, 2>));

// A class's operator template, as members_of finds it.
consteval auto operator_template_of(info cls) -> info {
  for (info m : members_of(cls, access_context::unchecked()))
    if (is_operator_function(m) && operator_of(m) == operators::op_template)
      return m;
  return {};
}
constexpr info op = operator_template_of(^^Frac);

// Its return type is std::meta::info.
static_assert(type_of(op) == ^^info() const);

// It is an operator function, and a special member function.
static_assert(is_operator_function(op));
static_assert(is_special_member_function(op));
static_assert(operator_of(op) == operators::op_template);
static_assert(symbol_of(operators::op_template) == "template");
static_assert(u8symbol_of(operators::op_template) == u8"template");
// (But not one lookup finds: it can be named only in its declaration.)
static_assert(!lookup(^^Frac, operators::op_template, access_context::unchecked())
                   .has_value());
static_assert(display_string_of(op) == "operator template");

// User-provided, defaulted, or deleted, it says so.
static_assert(is_user_provided(op) && !is_defaulted(op));

// Defaulted, with private members: no return type to spell.
class Priv {
  int x = 1;
  consteval operator template() const = default;

public:
  constexpr int get() const { return x; }
};
template <Priv P> constexpr int pv = P.get();
static_assert(pv<Priv{}> == 1);
static_assert(is_defaulted(operator_template_of(^^Priv)));

// Deleted: not structural.
struct NoNTTP {
  int x;
  consteval operator template() const = delete;
};
static_assert(!std::meta::is_structural_type(^^NoNTTP));
static_assert(is_deleted(operator_template_of(^^NoNTTP)));

// With an explicit object parameter.
struct E {
  int x;
  consteval operator template(this E const&) = default;
};
template <E e> constexpr int ev = e.x;
static_assert(ev<E{5}> == 5);

// The name reflect_constant is free again: a class may use it, and an
// unqualified call inside a member function finds std::meta's.
struct UsesTheName {
  int reflect_constant = 3;
  consteval auto f() const -> info {
    return std::meta::reflect_constant(reflect_constant);
  }
};
static_assert([:UsesTheName{}.f():] == 3);

namespace adl {
using namespace std::meta;
struct S {
  int v = 4;
  consteval auto f() const -> info { return reflect_constant(v); }
};
static_assert([:S{}.f():] == 4);
}  // namespace adl

int main() {}
