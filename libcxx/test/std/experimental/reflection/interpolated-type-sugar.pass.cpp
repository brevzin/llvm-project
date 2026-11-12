//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// Interpolating a reflected type keeps whatever sugar the reflection has --
// a type trait builtin, decltype, pack indexing, a qualified name, a deduced
// placeholder -- none of which is spelled by the tokens it lands in. Each
// must declare the type it stands for.

#include <meta>
#include <type_traits>

namespace N {
struct A {
  using T = int;
  struct B {};
};
} // namespace N

N::A::T f();
N::A::B g();

template <class T> using RemoveCV = __remove_cv(T);
template <class T> using Decl = decltype(T{} + 1);
template <class... Ts> using Second = Ts...[1];

constexpr auto deduced() { return N::A::B{}; }
constexpr decltype(auto) deduced_twice() { return deduced(); }
struct Deducer {
  static constexpr auto get() { return N::A::B{}; }
};
template <class T> constexpr decltype(auto) forward_get() { return T::get(); }

struct S {
  consteval {
    queue_injection(^^{
      using unary = \(dealias(^^RemoveCV<int const>));
      using decl = \(dealias(^^Decl<char>));
      using indexed = \(dealias(^^Second<char, long>));
      using qualified_typedef = \(^^decltype(f()));
      using qualified_class = \(^^decltype(g()));
      using placeholder = \(return_type_of(^^deduced_twice));
      using placeholder_call = \(^^decltype(deduced_twice()));
      using placeholder_template = \(^^decltype(forward_get<Deducer>()));

      // and not only as an alias: as a declarator's type, a return type, a
      // parameter type
      decl const* ptr;
      auto get(qualified_class) -> \(dealias(^^RemoveCV<long const>));
    });
  }
};

static_assert(std::is_same_v<S::unary, int>);
static_assert(std::is_same_v<S::decl, int>);
static_assert(std::is_same_v<S::indexed, long>);
static_assert(std::is_same_v<S::qualified_typedef, int>);
static_assert(std::is_same_v<S::qualified_class, N::A::B>);
static_assert(std::is_same_v<S::placeholder, N::A::B>);
static_assert(std::is_same_v<S::placeholder_call, N::A::B>);
static_assert(std::is_same_v<S::placeholder_template, N::A::B>);
static_assert(std::is_same_v<decltype(S::ptr), int const*>);
static_assert(std::is_same_v<decltype(&S::get), long (S::*)(N::A::B)>);

int main(int, char**) { return 0; }
