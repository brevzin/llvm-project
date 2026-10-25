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

// std::meta::hidden_friends_of(cls): the functions and function templates
// first declared by a friend declaration in cls, in declaration order. They
// are not members, and access does not apply to them.
//
// std::meta::interface_of(cls, ctx): cls's functional interface -- its member
// functions and member function templates accessible in ctx, other than
// constructors, destructors, and special member functions -- followed by its
// hidden friends.

#include <meta>
#include <string_view>
#include <vector>

using std::meta::info;
using std::meta::access_context;

// A name for each: its identifier, an operator's symbol, or "conversion".
consteval auto name_of(info r) -> std::string_view {
  if (has_identifier(r))
    return identifier_of(r);
  if (is_operator_function(r) || is_operator_function_template(r))
    return symbol_of(operator_of(r));
  if (is_conversion_function(r))
    return "conversion";
  return "?";
}
consteval auto names(std::vector<info> v) -> std::vector<std::string_view> {
  std::vector<std::string_view> out;
  for (info r : v)
    out.push_back(name_of(r));
  return out;
}

void declared_before(struct C const&);
struct Other { void member(); };
void later(struct C const&);  // (redeclared below, after the class)

struct C {
  C();
  C(int);
  C(C const&);
  C& operator=(C const&);
  ~C();
  C& operator=(int);              // not a special member function
  void f();
  static void s();
  template <class T> void tf(T);
  operator bool() const;

  friend bool operator==(C const&, C const&) { return true; }   // hidden
  template <class T> friend void tfriend(C const&, T) {}         // hidden
  friend void declared_only(C const&);                           // hidden
  friend void declared_before(C const&);    // not: declared before
  friend void Other::member();              // not: a member of another class
  friend struct Other;                      // not: a class
  friend void reopened(C const&) {}         // hidden: first declared here

private:
  void hidden_member();
};
void reopened(C const&);  // a later namespace-scope declaration: still listed

static_assert(names(hidden_friends_of(^^C)) ==
              std::vector<std::string_view>{"==", "tfriend", "declared_only",
                                            "reopened"});

// The reflections are of the friends themselves.
constexpr info eq = hidden_friends_of(^^C)[0];
static_assert(is_operator_function(eq) &&
              operator_of(eq) == std::meta::operators::op_equals_equals);
static_assert(parameters_of(eq).size() == 2);
static_assert(is_function_template(hidden_friends_of(^^C)[1]));
static_assert(!is_class_member(eq));

// interface_of: member functions (static, templates, conversion functions,
// non-special assignment) then hidden friends; no constructors, destructor,
// or special member functions. Access applies to the members only.
static_assert(names(interface_of(^^C, access_context::unprivileged())) ==
              std::vector<std::string_view>{"=", "f", "s", "tf", "conversion",
                                            "==", "tfriend", "declared_only",
                                            "reopened"});
static_assert(interface_of(^^C, access_context::unchecked()).size() ==
              interface_of(^^C, access_context::unprivileged()).size() + 1);

// A class template specialization's hidden friends.
template <class T>
struct Box {
  T value;
  friend constexpr bool operator==(Box const& a, Box const& b) {
    return a.value == b.value;
  }
  friend constexpr auto operator<=>(Box const&, Box const&) = default;
};
static_assert(hidden_friends_of(^^Box<int>).size() == 2);
static_assert(operator_of(hidden_friends_of(^^Box<int>)[1]) ==
              std::meta::operators::op_spaceship);

// While the class is being defined: the friends declared so far.
struct Partial {
  friend void first(Partial const&) {}
  consteval {
    if (hidden_friends_of(^^Partial).size() != 1)
      throw "expected one hidden friend so far";
  }
  friend void second(Partial const&) {}
};
static_assert(hidden_friends_of(^^Partial).size() == 2);

// No friends at all.
struct Plain { void f(); };
static_assert(hidden_friends_of(^^Plain).empty());

int main(int, char**) { return 0; }
