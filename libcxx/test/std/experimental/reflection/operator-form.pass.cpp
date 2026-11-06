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

// std::meta::operator_form_of: whether an operator function (or operator
// function template) implements a prefix expression (@x), a postfix
// expression (x@, including x(...), x[...], x->), or a binary expression
// (x @ y). It tells apart the forms of + - * & and of ++ --, whichever way
// the function is declared: as a member (with an implicit or an explicit
// object parameter), a static member, or a non-member (or hidden friend).

#include <meta>
#include <string_view>

using std::meta::access_context;
using std::meta::info;
using std::meta::operator_form;
using enum std::meta::operators;

// The nth operator function (or template) for 'op' among the members of X (a
// class or namespace), then its hidden friends.
consteval auto nth(info X, std::meta::operators op, int n) -> info {
  int i = 0;
  for (info m : members_of(X, access_context::unchecked()))
    if ((is_operator_function(m) || is_operator_function_template(m)) &&
        operator_of(m) == op && i++ == n)
      return m;
  if (is_class_type(X))
    for (info m : hidden_friends_of(X))
      if ((is_operator_function(m) || is_operator_function_template(m)) &&
          operator_of(m) == op && i++ == n)
        return m;
  return {};
}

consteval auto form(info X, std::meta::operators op, int n) -> operator_form {
  return operator_form_of(nth(X, op, n));
}

constexpr auto prefix = operator_form::prefix;
constexpr auto postfix = operator_form::postfix;
constexpr auto binary = operator_form::binary;

// ---------------------------------------- implicit object member functions ----

struct M {
  M operator-() const;
  M operator-(M) const;
  M operator*() const;
  M operator*(M) const;
  M* operator&();
  M operator&(M) const;
  M operator+() const;
  M operator+(M) const;
  M& operator++();
  M operator++(int);
  M& operator--();
  M operator--(int);
};
static_assert(form(^^M, op_minus, 0) == prefix);
static_assert(form(^^M, op_minus, 1) == binary);
static_assert(form(^^M, op_star, 0) == prefix);
static_assert(form(^^M, op_star, 1) == binary);
static_assert(form(^^M, op_ampersand, 0) == prefix);
static_assert(form(^^M, op_ampersand, 1) == binary);
static_assert(form(^^M, op_plus, 0) == prefix);
static_assert(form(^^M, op_plus, 1) == binary);
static_assert(form(^^M, op_plus_plus, 0) == prefix);
static_assert(form(^^M, op_plus_plus, 1) == postfix);
static_assert(form(^^M, op_minus_minus, 0) == prefix);
static_assert(form(^^M, op_minus_minus, 1) == postfix);

// ---------------------------------------- explicit object member functions ----

struct E {
  E operator-(this E const&);
  E operator-(this E const&, E);
  E& operator++(this E&);
  E operator++(this E&, int);
};
static_assert(form(^^E, op_minus, 0) == prefix);
static_assert(form(^^E, op_minus, 1) == binary);
static_assert(form(^^E, op_plus_plus, 0) == prefix);
static_assert(form(^^E, op_plus_plus, 1) == postfix);

// ---------------------------------------------------------- hidden friends ----

struct F {
  friend F operator-(F);
  friend F operator-(F, F);
  friend F& operator++(F&);
  friend F operator++(F&, int);
};
static_assert(form(^^F, op_minus, 0) == prefix);
static_assert(form(^^F, op_minus, 1) == binary);
static_assert(form(^^F, op_plus_plus, 0) == prefix);
static_assert(form(^^F, op_plus_plus, 1) == postfix);

// ------------------------------------------------------- namespace scope ----

namespace ns {
struct N {};
N operator*(N);
N operator*(N, N);
N& operator--(N&);
N operator--(N&, int);
}  // namespace ns
static_assert(form(^^ns, op_star, 0) == prefix);
static_assert(form(^^ns, op_star, 1) == binary);
static_assert(form(^^ns, op_minus_minus, 0) == prefix);
static_assert(form(^^ns, op_minus_minus, 1) == postfix);

// --------------------------------------------- operators with one form ----

struct One {
  bool operator!() const;
  One operator~() const;
  One operator=(One const&);
  One& operator+=(One);
  bool operator==(One const&) const;
  auto operator<=>(One const&) const = default;
  One operator,(One) const;
  One* operator->();
  int operator->*(int) const;
  int operator()(int, int) const;
  int operator()() const;
  int operator[](int) const;
  int operator[]() const;
  int operator[](int, int) const;
  bool operator&&(One) const;
};
static_assert(form(^^One, op_exclamation, 0) == prefix);
static_assert(form(^^One, op_tilde, 0) == prefix);
static_assert(form(^^One, op_equals, 0) == binary);
static_assert(form(^^One, op_plus_equals, 0) == binary);
static_assert(form(^^One, op_equals_equals, 0) == binary);
static_assert(form(^^One, op_spaceship, 0) == binary);
static_assert(form(^^One, op_comma, 0) == binary);
static_assert(form(^^One, op_arrow, 0) == postfix);
static_assert(form(^^One, op_arrow_star, 0) == binary);
static_assert(form(^^One, op_parentheses, 0) == postfix);
static_assert(form(^^One, op_parentheses, 1) == postfix);
static_assert(form(^^One, op_square_brackets, 0) == postfix);
static_assert(form(^^One, op_square_brackets, 1) == postfix);
static_assert(form(^^One, op_square_brackets, 2) == postfix);
static_assert(form(^^One, op_ampersand_ampersand, 0) == binary);

// Static call and subscript operators: no object parameter, still postfix.
struct S {
  static int operator()(int);
  static int operator[](int);
};
static_assert(form(^^S, op_parentheses, 0) == postfix);
static_assert(form(^^S, op_square_brackets, 0) == postfix);

// ------------------------------------------------ function templates ----

struct T {
  template <class U> T operator-(U) const;      // binary
  template <class U> friend T operator-(U, T);  // binary
  template <class U = int> T operator*() const; // prefix
  template <class U> friend T operator+(U);     // prefix
};
static_assert(is_operator_function_template(nth(^^T, op_minus, 0)));
static_assert(form(^^T, op_minus, 0) == binary);
static_assert(form(^^T, op_minus, 1) == binary);
static_assert(form(^^T, op_star, 0) == prefix);
static_assert(form(^^T, op_plus, 0) == prefix);

// --------------------------------------------- the motivating questions ----

consteval auto is_binary_minus(info f) -> bool {
  return operator_of(f) == op_minus && operator_form_of(f) == binary;
}
consteval auto is_postfix_increment(info f) -> bool {
  return operator_of(f) == op_plus_plus && operator_form_of(f) == postfix;
}
static_assert(!is_binary_minus(nth(^^M, op_minus, 0)));
static_assert(is_binary_minus(nth(^^E, op_minus, 1)));
static_assert(is_binary_minus(nth(^^F, op_minus, 1)));
static_assert(!is_postfix_increment(nth(^^E, op_plus_plus, 0)));
static_assert(is_postfix_increment(nth(^^F, op_plus_plus, 1)));

int main() {}
