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

// Explicit template arguments at a macro invocation, 'name!<Args>(...)', in
// every position a macro can be invoked. The '!' comes first, so the parser
// knows it is reading a macro invocation before it reaches the '<' (and no
// 'template' keyword is ever needed). Only macro templates are candidates.

#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

// --------------------------------------------------- kinds of argument ----

template <class T>
__macro size_of() { return ^^{ sizeof(\(^^T)) }; }
static_assert(size_of!<int>() == sizeof(int));

template <int N>
__macro twice() { return ^^{ \(std::meta::reflect_constant(2 * N)) }; }
static_assert(twice!<21>() == 42);

template <template <class...> class C>
__macro make_int_of() { return ^^{ \(^^C)<int>{} }; }
static_assert(make_int_of!<std::vector>().empty());

namespace ns {
template <class T>
__macro zero() { return ^^{ \(^^T){} }; }
} // namespace ns

template <std::meta::info R>
__macro name_of() {
  return ^^{ \(std::meta::reflect_constant_string(identifier_of(R))) };
}
static_assert(name_of!<^^ns>()[0] == 'n');

template <class... Ts>
__macro count() { return ^^{ \(std::meta::reflect_constant(sizeof...(Ts))) }; }
static_assert(count!<int, long, char>() == 3);

// '>>' closes both lists.
static_assert(size_of!<std::vector<std::vector<int>>>() ==
              sizeof(std::vector<std::vector<int>>));

// Explicit and deduced together; any bracket after the template arguments.
template <class To>
__macro as(auto&& e) { return ^^{ static_cast<\(^^To)>(\(e)) }; }
static_assert(as!<int>(3.9) == 3);
static_assert(as!<int>{3.9} == 3 && as!<int>[4.2] == 4);

// With explicit template arguments only the templates are candidates.
__macro pick() { return ^^{ 0 }; }
template <class T>
__macro pick() { return ^^{ 1 }; }
static_assert(pick!() == 0);
static_assert(pick!<int>() == 1);

// --------------------------------------------------------- positions ------

// Qualified.
static_assert(ns::zero!<int>() == 0);

// A dependent template argument defers the expansion to instantiation.
template <class T>
constexpr std::size_t dep() { return size_of!<T>(); }
static_assert(dep<char>() == 1 && dep<double>() == 8);

// A dependent qualifier: the macro is looked up at instantiation.
struct Lib {
  template <class T>
  static __macro size_of() { return ^^{ sizeof(\(^^T)) }; }
};
template <class L>
constexpr auto dep_qual() { return L::size_of!<short>(); }
static_assert(dep_qual<Lib>() == sizeof(short));

// Declaration macro, at namespace scope...
template <class E>
  requires std::is_enum_v<E>
__macro bitmask_type() {
  std::meta::list_builder ops;
  using enum std::meta::operators;
  for (auto op : {op_pipe, op_ampersand, op_caret})
    ops += ^^{
      constexpr auto operator \(op)(\(^^E) lhs, \(^^E) rhs) -> \(^^E) {
        return \(^^E)(std::to_underlying(lhs) \(op) std::to_underlying(rhs));
      }
    };
  return ^^{ \(ops) };
}

namespace N {
enum class Permission : int { None = 0, Read = 1, Write = 2, Execute = 4 };
bitmask_type!<Permission>();
} // namespace N
static_assert((N::Permission::Read | N::Permission::Write) == N::Permission(3));
static_assert((N::Permission(7) & N::Permission::Write) == N::Permission::Write);

// ...and in a class template, per specialization.
template <class T>
__macro member_of_type() { return ^^{ \(^^T) value{}; }; }
template <class T>
struct Box {
  member_of_type!<T>();
};
static_assert(std::is_same_v<decltype(Box<long>::value), long>);

// Statement macro, also with a dependent template argument.
template <class T>
__macro declare_zero() { return ^^{ \(^^T) z = 0 }; }
constexpr int stmt() {
  declare_zero!<int>();
  return z + 5;
}
static_assert(stmt() == 5);

template <class T>
__macro bump(auto&& x) { return ^^{ \(x) += \(^^T)(1) }; }
template <class T>
constexpr T dep_stmt() {
  T v = 0;
  bump!<T>(v);
  return v;
}
static_assert(dep_stmt<long>() == 1);

// Member macro: on an object, on a dependent object, and on implicit 'this'.
struct S {
  template <class T>
  __macro get(this auto&& self) {
    return ^^{ static_cast<\(^^T)>(\(self).v) };
  }
  double v = 2.5;
  constexpr int f() const { return get!<int>(); }
};
static_assert(S{}.get!<int>() == 2);
template <class U>
constexpr int via(U u) { return u.get!<int>(); }
static_assert(via(S{}) == 2);
static_assert(S{}.f() == 2);

// Mem-initializer macro, in a class and in a class template.
template <class T>
__macro init_v() { return ^^{ v(\(^^T){7}) }; }
struct M {
  int v;
  constexpr M() : init_v!<int>() {}
};
static_assert(M().v == 7);
template <class X>
struct Holder {
  X v;
  constexpr Holder() : init_v!<X>() {}
};
static_assert(Holder<long>().v == 7);

int main(int, char**) { return 0; }
