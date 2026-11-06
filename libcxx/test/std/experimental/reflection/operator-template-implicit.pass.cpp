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

// P4340 ext: a class that doesn't declare an 'operator template' has one
// declared implicitly, as for the copy constructor: an inline public consteval
// member that is defaulted -- or defined as deleted if the class has a direct
// base class or non-static data member that is not public, or whose type isn't
// structural. (A mutable member makes the class non-structural without
// deleting it.) A user-declared one suppresses it, even one whose constraints
// aren't satisfied: then the class has no eligible operator template, and
// isn't structural. members_of shows the implicit one; nothing else changes.

#include <meta>
#include <memory>
#include <vector>

using namespace std::meta;

consteval auto op_of(info cls) -> info {
  for (info m : members_of(cls, access_context::unchecked()))
    if (is_operator_function(m) && operator_of(m) == operators::op_template)
      return m;
  return {};
}
consteval int count_ops(info cls) {
  int n = 0;
  for (info m : members_of(cls, access_context::unchecked()))
    if (is_operator_function(m) && operator_of(m) == operators::op_template) ++n;
  return n;
}

struct Pub { int x; };                         // defaulted
class Priv { [[maybe_unused]] int x; };        // deleted: private member
struct HasPrivBase : private Pub {};           // deleted: non-public base
struct HasPtr { int* p; };                     // defaulted (pointers are structural)
struct HasRef { int& r; };                     // defaulted? (lvalue ref is structural)
struct HasRRef { int&& r; };                   // deleted: rvalue ref
struct HasNonStruct { Priv p; };               // deleted: subobject not structural
struct HasArr { Pub a[2]; };                   // defaulted
struct Mut { mutable int x; };                 // defaulted, but not structural (mutable)
union U { int i; float f; };                   // defaulted
struct WithAnon { union { int a; float b; }; };
struct User { int x; consteval operator template() const = default; };

static_assert(op_of(^^Pub) != info{} && is_defaulted(op_of(^^Pub)) && !is_deleted(op_of(^^Pub)));
static_assert(!is_user_provided(op_of(^^Pub)) && is_special_member_function(op_of(^^Pub)));
static_assert(is_deleted(op_of(^^Priv)));
static_assert(is_deleted(op_of(^^HasPrivBase)));
static_assert(!is_deleted(op_of(^^HasPtr)));
static_assert(!is_deleted(op_of(^^HasRef)) == is_structural_type(^^HasRef));
static_assert(is_deleted(op_of(^^HasRRef)));
static_assert(is_deleted(op_of(^^HasNonStruct)));
static_assert(!is_deleted(op_of(^^HasArr)));
static_assert(!is_deleted(op_of(^^Mut)) && !is_structural_type(^^Mut));
static_assert(!is_deleted(op_of(^^U)));
static_assert(count_ops(^^WithAnon) == 1);
static_assert(count_ops(^^User) == 1 && !is_user_provided(op_of(^^User)) && is_defaulted(op_of(^^User)));
static_assert(is_public(op_of(^^Priv)));
static_assert(type_of(op_of(^^Pub)) == ^^info() const);

// the implicit one agrees with is_structural_type (absent mutable members)
template <class... Ts> consteval bool agrees() {
  return ((is_deleted(op_of(^^Ts)) == !is_structural_type(^^Ts)) && ...);
}
static_assert(agrees<Pub, Priv, HasPrivBase, HasPtr, HasRef, HasRRef, HasNonStruct, HasArr, U>());

// library types: user-declared (possibly constrained) ones suppress it
static_assert(count_ops(^^std::vector<int>) == 1);
template <class T> struct A2 : std::allocator<T> { template <class U> struct rebind { using other = A2<U>; }; };
static_assert(count_ops(^^std::vector<int, A2<int>>) == 0);   // user-declared but not eligible: suppresses the implicit one
static_assert(!is_structural_type(^^std::vector<int, A2<int>>));

// class template specialization, closure type
template <class T> struct Box { T t; };
static_assert(!is_deleted(op_of(^^Box<int>)) && is_deleted(op_of(^^Box<Priv>)));
using Lambda = decltype([] {});
static_assert(op_of(^^Lambda) != info{});

// templates still work as before
template <Pub P> constexpr int pv = P.x;
static_assert(pv<Pub{4}> == 4);
static_assert(display_string_of(op_of(^^Pub)) == "operator template");

int main() {}
