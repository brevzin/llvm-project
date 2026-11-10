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

// std::meta::is_closure_type(r) is whether r is a closure type: the type of a
// lambda-expression, cv-qualified or not, named directly or through an alias.

#include <meta>

using std::meta::info;

constexpr auto lam = [](int x) { return x; };
constexpr auto generic = [](auto x) { return x; };
using Alias = decltype(lam);

struct S {};
union U {};
enum E {};

static_assert(is_closure_type(^^decltype(lam)));
static_assert(is_closure_type(^^decltype(generic)));
static_assert(is_closure_type(^^Alias));
static_assert(is_closure_type(^^const decltype(lam)));
static_assert(is_closure_type(^^const volatile Alias));
static_assert(is_closure_type(type_of(^^lam)));

static_assert(!is_closure_type(^^S));
static_assert(!is_closure_type(^^U));
static_assert(!is_closure_type(^^E));
static_assert(!is_closure_type(^^int));
static_assert(!is_closure_type(^^int (*)(int)));
static_assert(!is_closure_type(^^decltype(lam)&));
static_assert(!is_closure_type(^^decltype(lam)*));
static_assert(!is_closure_type(^^decltype(lam)[2]));
static_assert(!is_closure_type(^^decltype(+lam)));

// A class derived from a closure type is not a closure type.
struct Derived : decltype(lam) {};
static_assert(!is_closure_type(^^Derived));

// Closure types in templates.
template <class T> constexpr auto in_template = [](T t) { return t; };
static_assert(is_closure_type(^^decltype(in_template<int>)));

template <class T> consteval info closure_in_function_template() {
  auto l = [](T) {};
  return ^^decltype(l);
}
static_assert(is_closure_type(closure_in_function_template<int>()));

template <class T> constexpr bool trait = is_closure_type(^^T);
static_assert(trait<decltype(generic)>);
static_assert(!trait<S>);

// The motivating use: skipping lambdas when walking out from the current
// scope to find the function (or class or namespace) that encloses them.
consteval info skip_closures(info scope) {
  while (is_function(scope) && is_type(parent_of(scope)) &&
         is_closure_type(parent_of(scope)))
    scope = parent_of(parent_of(scope));
  return scope;
}

struct Logger {
  static constexpr info in_member_init =
      [] { return std::meta::access_context::current().scope(); }();

  void f() {
    static_assert(skip_closures(std::meta::access_context::current().scope()) ==
                  ^^Logger::f);
    [&] {
      static_assert(is_closure_type(
          parent_of(std::meta::access_context::current().scope())));
      [&]<class T>(T) {
        static_assert(skip_closures(
                          std::meta::access_context::current().scope()) ==
                      ^^Logger::f);
      }(1);
    }();
  }

  void g(this Logger const&) {
    [] {
      static_assert(skip_closures(
                        std::meta::access_context::current().scope()) ==
                    ^^Logger::g);
    }();
  }
};
static_assert(skip_closures(Logger::in_member_init) == ^^Logger);

constexpr auto at_namespace_scope = [] {
  return std::meta::access_context::current().scope();
};
static_assert(skip_closures(at_namespace_scope()) == ^^::);

void free_function() {
  [] {
    static_assert(skip_closures(std::meta::access_context::current().scope()) ==
                  ^^free_function);
  }();
}

int main() {
  Logger l;
  l.f();
  l.g();
}
