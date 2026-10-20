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

// '$' interpolation in a token sequence, alongside '\( )':
//   $name          interpolates the name (an identifier, nothing after it)
//   ${ e }         interpolates an expression
//   ${ e...; sep } the list form, as \( e...; sep )
//   $$name         one '$' per enclosing literal, as with backslashes
//   \$name         the identifier '$name'
// Only tokens written in a literal are examined: a macro's raw argument is
// passed through as written.

#include <meta>
#include <string_view>

using namespace std::meta;

constexpr int sum() { return 0; }
template <class... T> constexpr int sum(int a, T... t) { return a + sum(t...); }

// $name, with the macro's parameters.
template <class A, class B> __macro add(A&& a, B&& b) { return ^^{ ($a + $b) }; }
static_assert(add!(1, 2) == 3);

// Only the name is interpolated; what follows is tokens.
struct P { int x; constexpr int twice() const { return 2 * x; } };
template <class S> __macro twice_x(S&& s) { return ^^{ $s.twice() }; }
static_assert(twice_x!(P{4}) == 8);

// ${ e }: any expression.
consteval int seven() { return 7; }
__macro lucky() { return ^^{ ${ reflect_constant(seven()) } }; }
static_assert(lucky!() == 7);

// Lists and separators.
__macro sum_of(token_sequence... xs) { return ^^{ sum(${ xs... }) }; }
static_assert(sum_of!(1, 2, 3) == 6);
static_assert(sum_of!() == 0);
__macro all(token_sequence... xs) { return ^^{ (${ xs...; ^^{ && } }) }; }
static_assert(all!(true, 1 < 2));
static_assert(!all!(true, false));
__macro around(token_sequence... xs) {
  return ^^{ sum(${ ^^{ 100 }, xs..., ^^{ 1000 } }) };
}
static_assert(around!(1, 2) == 1103);

// Both spellings in one literal.
template <class A, class B> __macro mixed(A&& a, B&& b) {
  return ^^{ sum($a, \(b), ${ ^^{ 10 } }) };
}
static_assert(mixed!(1, 2) == 13);

// Nested literals: a macro that defines a macro. '$$x' is the outer
// literal's; '$y' stays for the inner one.
consteval {
  token_sequence k = ^^{ 40 };
  queue_injection(^^{
    __macro plus_k(token_sequence y) { return ^^{ ($y + $$k) }; }
  });
}
static_assert(plus_k!(2) == 42);

// \$x is the identifier '$x'.
__macro declare_dollar(token_sequence value) {
  return ^^{ constexpr int \$v = $value; };
}
declare_dollar!(5);
static_assert($v == 5);
consteval bool escaped() {
  token_sequence t = ^^{ \$v };
  return t.size() == 1 && identifier_of(t[0]) == "$v";
}
static_assert(escaped());
// ... as is an identifier id() makes, when interpolated.
consteval bool from_id() {
  token_sequence t = ^^{ ${ id("$v") } };
  return t.size() == 1 && identifier_of(t[0]) == "$v";
}
static_assert(from_id());

// A raw argument is not a literal: '$1' arrives as the identifier '$1'.
__macro first_is_dollar1(token_sequence body) {
  bool b = token_kind_of(body[0]) == token_kind::identifier &&
           identifier_of(body[0]) == "$1";
  return ^^{ ${ reflect_constant(b) } };
}
static_assert(first_is_dollar1!($1.x > $2.x));

int main() {}
