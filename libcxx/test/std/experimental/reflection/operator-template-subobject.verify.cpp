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

// P4340 ext: a subobject returned by a user-provided 'operator template' must
// be of type 'const C', usable in constant expressions, and idempotent.

#include <meta>

// Not of type 'const C': the whole object instead of the subobject.
struct WholeObject {
  int v;
  consteval operator template() const;
};
struct Wrapper {
  WholeObject w;
};
constexpr Wrapper wrapper{{1}};
consteval WholeObject::operator template() const { return std::meta::reflect_object(wrapper); }
template <WholeObject>
struct Y0 {};
Y0<WholeObject{1}> y0;
// expected-error@-1 {{'operator template' for 'WholeObject' returned a reflection of an object of type 'const Wrapper' rather than 'const WholeObject'}}

// An inactive union member.
struct Inactive {
  int v;
  consteval operator template() const;
};
union Store {
  Inactive a;
  Inactive b;
};
constexpr Store store{.a = {1}};
consteval Inactive::operator template() const { return std::meta::reflect_object(store.b); }
template <Inactive>
struct Y1 {};
Y1<Inactive{1}> y1;
// expected-error@-1 {{'operator template' for 'Inactive' returned a reflection of an object not usable in constant expressions}}

// A mutable member.
struct Mutable {
  int v;
  consteval operator template() const;
};
struct HasMutable {
  mutable Mutable m;
};
constexpr HasMutable has_mutable{{1}};
consteval Mutable::operator template() const { return std::meta::reflect_object(has_mutable.m); }
template <Mutable>
struct Y2 {};
Y2<Mutable{1}> y2;
// expected-error@-1 {{'operator template' for 'Mutable' returned a reflection of an object not usable in constant expressions}}

// Not idempotent: the subobject's own call returns a different subobject.
struct Flip {
  int v;
  consteval operator template() const;
};
struct Pair {
  Flip a, b;
};
constexpr Pair pair{{0}, {1}};
consteval Flip::operator template() const {
  return v == 0 ? std::meta::reflect_object(pair.b) : std::meta::reflect_object(pair.a);
}
template <Flip>
struct Y3 {};
Y3<Flip{0}> y3;
// expected-error@-1 {{'operator template' for 'Flip' is not idempotent: calling it on its own result returned a reflection of a different object}}

// A subobject of a variable that isn't usable in constant expressions.
struct NotConstant {
  int v;
  consteval operator template() const;
};
struct Box {
  NotConstant n;
};
Box box{{1}};
consteval NotConstant::operator template() const { return std::meta::reflect_object(box.n); }
template <NotConstant>
struct Y4 {};
Y4<NotConstant{1}> y4;
// expected-error@-1 {{'operator template' for 'NotConstant' returned a reflection of an object not usable in constant expressions}}
