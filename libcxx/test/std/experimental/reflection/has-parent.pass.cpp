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

// std::meta::has_parent: false for the global namespace, for entities with C
// language linkage, for types that are neither class nor enumeration types,
// and for reflections of anything other than an entity or a direct base class
// relationship; true otherwise. It is true exactly when parent_of(r) is a
// constant expression.

#include <meta>
#include <type_traits>

using std::meta::access_context;
using std::meta::info;

// Whether parent_of(R) is a constant expression.
template <info R>
concept ParentIsConstant =
    requires { typename std::integral_constant<info, parent_of(R)>; };

template <info R, bool Expected>
consteval bool check() {
  static_assert(has_parent(R) == Expected);
  static_assert(ParentIsConstant<R> == Expected);
  return true;
}

namespace ns {
struct Cls {
  int mem;
  static int smem;
  void memfn(int param);
  struct Inner {};
  enum E { A };
  enum class SE { B };
  union {
    int anon;
  };
  template <class T>
  void tmemfn();
};
struct Derived : Cls {};
union U {};
enum Enum { X };
using Alias = int;
int var;
void fn(int p);
template <class T>
struct TCls {};
template <class T>
using TAlias = TCls<T>;
inline namespace in {}
namespace nested {}
inline auto lambda = [] {};

extern "C" void c_fn();
extern "C" int c_var;
extern "C++" void cpp_fn();
extern "C" {
void c_block_fn();
}
} // namespace ns

constexpr auto ctx = access_context::unchecked();

// Namespaces: everything but the global namespace has a parent.
static_assert(check<^^::, false>());
static_assert(check<^^ns, true>());
static_assert(check<^^ns::in, true>());
static_assert(check<^^ns::nested, true>());

// C language linkage has no parent; C++ language linkage does.
static_assert(check<^^ns::c_fn, false>());
static_assert(check<^^ns::c_var, false>());
static_assert(check<^^ns::c_block_fn, false>());
static_assert(check<^^ns::cpp_fn, true>());

// Types: only class and enumeration types (and type aliases, which are
// entities) have a parent.
static_assert(check<^^int, false>());
static_assert(check<^^int*, false>());
static_assert(check<^^int&, false>());
static_assert(check<^^int[3], false>());
static_assert(check<^^void(int), false>());
static_assert(check<^^ns::Cls, true>());
static_assert(check<^^ns::U, true>());
static_assert(check<^^ns::Enum, true>());
static_assert(check<^^ns::Cls::E, true>());
static_assert(check<^^ns::Cls::SE, true>());
static_assert(check<^^ns::Alias, true>());
static_assert(check<^^ns::TCls<int>, true>());
static_assert(check<type_of(^^ns::lambda), true>());

// Other entities.
static_assert(check<^^ns::var, true>());
static_assert(check<^^ns::fn, true>());
static_assert(check<^^ns::TCls, true>());
static_assert(check<^^ns::TAlias, true>());
static_assert(check<^^ns::Cls::mem, true>());
static_assert(check<^^ns::Cls::smem, true>());
static_assert(check<^^ns::Cls::memfn, true>());
static_assert(check<^^ns::Cls::Inner, true>());
static_assert(check<^^ns::Cls::tmemfn, true>());
static_assert(check<^^ns::Cls::A, true>());
static_assert(check<^^ns::Cls::SE::B, true>());
static_assert(check<^^ns::X, true>());
static_assert(check<^^ns::Cls::anon, true>());
static_assert(check<parameters_of(^^ns::fn)[0], true>());
static_assert(check<parameters_of(^^ns::Cls::memfn)[0], true>());

// A direct base class relationship has a parent: the derived class.
static_assert(check<bases_of(^^ns::Derived, ctx)[0], true>());
static_assert(parent_of(bases_of(^^ns::Derived, ctx)[0]) == ^^ns::Derived);

// Anything that is not an entity or a direct base class relationship.
static_assert(check<info{}, false>());
static_assert(check<std::meta::reflect_constant(42), false>());
static_assert(check<std::meta::reflect_object(ns::var), false>());
static_assert(check<data_member_spec(^^int, {.name = "m"}), false>());

int main() {}
