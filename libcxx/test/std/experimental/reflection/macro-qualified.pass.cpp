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

// Qualified macro invocations in declaration and statement position,
// 'ns::name!(args);', as in expression position: at namespace and class scope
// and as a statement, with any nested-name-specifier, and with a dependent
// qualifier (the macro, and the shape of its arguments, are then known only at
// instantiation).

#include <meta>
#include <type_traits>

namespace lib {
  template <class T> __macro declare(std::meta::token_sequence name) {
    return ^^{ \(^^T) \(name) {}; };
  }
  __macro make_int(std::meta::token_sequence name) { return ^^{ constexpr int \(name) = 1; }; }
  namespace inner { __macro make_long(std::meta::token_sequence name) { return ^^{ constexpr long \(name) = 2; }; } }
  __macro add(auto&& x, auto&& y) { return ^^{ \(x) += \(y) }; }
  __macro twice(auto&& x) { return ^^{ (\(x) * 2) }; }
  template <class T> struct Holder { static __macro value_member() { return ^^{ \(^^T) held{}; }; } };
}

// Namespace scope: simple, global, nested, template-id qualifiers, template args.
namespace A {
  lib::make_int!(a1);
  ::lib::make_int!(a2);
  lib::inner::make_long!(a3);
  lib::declare!<double>(a4);
}
static_assert(A::a1 == 1 && A::a2 == 1 && A::a3 == 2);
static_assert(std::is_same_v<decltype(A::a4), double>);

// Class scope, a class template with a non-dependent qualifier, and with a
// dependent one.
struct C { lib::Holder<char>::value_member!(); };
static_assert(std::is_same_v<decltype(C::held), char>);
template <class T> struct D { lib::declare!<T>(d); };
static_assert(std::is_same_v<decltype(D<short>::d), short>);
struct Policy { static __macro member() { return ^^{ int from_policy = 3; }; } };
template <class P> struct E { P::member!(); };
static_assert(E<Policy>{}.from_policy == 3);

// Statement position, qualified; and still an expression when not the
// whole statement.
constexpr int stmt() {
  int x = 1;
  lib::add!(x, 4);
  ::lib::add!(x, 1);
  return x;
}
static_assert(stmt() == 6);
constexpr int expr_stmt() {
  int x = 3;
  (void)(lib::twice!(x) + 1);
  return lib::twice!(x);
}
static_assert(expr_stmt() == 6);

// Statement with a dependent qualifier.
struct Ops { static __macro bump(auto&& x) { return ^^{ ++\(x) }; } };
template <class O> constexpr int dep_stmt() { int v = 0; O::bump!(v); O::bump!(v); return v; }
static_assert(dep_stmt<Ops>() == 2);

// Ordinary qualified code is untouched.
namespace X { struct Y { static int f(); }; using Z = int; }
X::Z z1 = 0;
int X::Y::f() { return 0; }
void g() {}
void h() { X::Y::f(); ::g(); X::Z local = 1; (void)local; }

int main(int, char**) { return 0; }
