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

// A function whose type contains an undeduced placeholder isn't
// members-of-representable. A member function of a class template
// specialization (or of a member class of one) has its placeholder return
// type deduced only when its definition is instantiated. members_of,
// interface_of, and hidden_friends_of deduce it, so the result doesn't depend
// on whether the specialization had already been instantiated before the
// query.

#include <meta>
#include <string>
#include <string_view>

using std::meta::access_context;

template <class T>
struct V {
  T x;
  auto operator-(V const& o) const { return V{x - o.x}; }
  decltype(auto) get() const { return (x); }
  auto plain() const -> int { return 0; }
  friend auto operator*(V a, V b) { return a.x * b.x; }
};

template <class T>
struct Outer {
  struct N {
    auto operator-(N) const { return T{}; }
  };
};

// Implicitly instantiates V<int> (but not the definitions of its members)
// before any query.
V<int> already_complete;

// The name of a function: its identifier, or "operator" followed by the
// operator's symbol.
consteval auto name_of(std::meta::info f) -> std::string {
  if (has_identifier(f))
    return std::string(identifier_of(f));
  if (is_operator_function(f))
    return "operator" + std::string(symbol_of(operator_of(f)));
  return "";
}

consteval auto named(std::meta::info cls, std::string_view name)
    -> std::meta::info {
  for (auto m : members_of(cls, access_context::unchecked()))
    if (is_function(m) && name_of(m) == name)
      return m;
  return {};
}

consteval bool has_friend(std::meta::info cls, std::string_view name) {
  for (auto f : hidden_friends_of(cls))
    if (name_of(f) == name)
      return true;
  return false;
}

consteval bool in_interface(std::meta::info cls, std::meta::info m) {
  for (auto f : interface_of(cls, access_context::unchecked()))
    if (f == m)
      return true;
  return false;
}

// Already instantiated before the query.
static_assert(named(^^V<int>, "operator-") != std::meta::info{});
static_assert(type_of(named(^^V<int>, "operator-")) ==
              ^^auto(const V<int>&) const->V<int>);
static_assert(return_type_of(named(^^V<int>, "get")) == ^^const int&);
static_assert(named(^^V<int>, "plain") != std::meta::info{});
static_assert(in_interface(^^V<int>, named(^^V<int>, "operator-")));
static_assert(has_friend(^^V<int>, "operator*"));
static_assert(return_type_of(hidden_friends_of(^^V<int>)[0]) == ^^int);

// First instantiated by the query.
static_assert(named(^^V<long>, "operator-") != std::meta::info{});
static_assert(return_type_of(named(^^V<long>, "operator-")) == ^^V<long>);
static_assert(has_friend(^^V<long>, "operator*"));

// A member class of a class template specialization.
static_assert(return_type_of(named(^^Outer<char>::N, "operator-")) == ^^char);

// A non-template function declared with a placeholder return type and not
// yet defined is (still) not members-of-representable.
struct D {
  auto f() const;
  auto g() const { return 1; }
};
static_assert(named(^^D, "f") == std::meta::info{});
static_assert(named(^^D, "g") != std::meta::info{});
auto D::f() const { return 1; }
static_assert(named(^^D, "f") != std::meta::info{});

int main() {}
