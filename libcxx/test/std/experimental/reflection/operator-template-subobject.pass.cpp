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

// P4340 ext: a user-provided 'operator template' can return a subobject of a
// variable's object -- a member, a base class subobject, or an array element
// -- so that related types can share one variable (and one symbol) for their
// template parameter objects.

#include <meta>
#include <type_traits>

template <auto x>
constexpr const auto* addr = &x;

// One variable per date. A date's template parameter object is the YMD inside
// it, and a year's is the Year inside January 1's YMD.
struct Year {
  int y;
  consteval operator template() const;
};
struct YMD {
  Year y;
  int m, d;
  consteval operator template() const;
};
struct Bundle {
  YMD ymd;
  int weekday;
};
template <int Yr, int M, int D>
constexpr Bundle bundle{{{Yr}, M, D}, (Yr + M + D) % 7};

consteval const Bundle& bundle_for(int y, int m, int d) {
  return std::meta::extract<const Bundle&>(
      substitute(^^bundle, {std::meta::reflect_constant(y), std::meta::reflect_constant(m),
                            std::meta::reflect_constant(d)}));
}
consteval YMD::operator template() const { return std::meta::reflect_object(bundle_for(y.y, m, d).ymd); }
consteval Year::operator template() const { return std::meta::reflect_object(bundle_for(y, 1, 1).ymd.y); }

static_assert(addr<Year{2024}> == &bundle<2024, 1, 1>.ymd.y);
static_assert(addr<YMD{{2024}, 3, 5}> == &bundle<2024, 3, 5>.ymd);
static_assert(addr<Year{2024}>->y == 2024);

// reflect_constant and template_arguments_of agree: a reflection of the
// subobject.
template <Year>
struct X {};
static_assert(std::meta::reflect_constant(Year{2024}) == std::meta::reflect_object(bundle<2024, 1, 1>.ymd.y));
static_assert(template_arguments_of(^^X<Year{2024}>)[0] == std::meta::reflect_constant(Year{2024}));
static_assert(std::meta::is_object(template_arguments_of(^^X<Year{2024}>)[0]));
static_assert(std::is_same_v<X<Year{2024}>, X<Year{2024}>>);
static_assert(!std::is_same_v<X<Year{2024}>, X<Year{2025}>>);

// Deduction from a specialization whose argument is a subobject.
template <Year y>
constexpr int year_of(X<y>) {
  return y.y;
}
static_assert(year_of(X<Year{1999}>{}) == 1999);

// Passing the parameter on to another template.
template <Year y>
struct Outer {
  using Inner = X<y>;
};
static_assert(std::is_same_v<Outer<Year{2024}>::Inner, X<Year{2024}>>);

// A Year member of a structural aggregate is normalized to a copy of the
// subobject.
struct Holder {
  Year y;
  int n;
};
template <Holder h>
constexpr int holder_year = h.y.y;
static_assert(holder_year<Holder{{2030}, 1}> == 2030);

// A base class subobject, even of a polymorphic class: the template parameter
// object is the Base subobject of a Derived object.
struct Base {
  int v;
  constexpr Base(int v) : v(v) {}
  constexpr virtual int f() const { return 1; }
  consteval operator template() const;
};
struct Derived : Base {
  constexpr Derived(int v) : Base(v) {}
  constexpr int f() const override { return 2; }
};
template <int V>
constexpr Derived derived{V};
consteval Base::operator template() const {
  return std::meta::reflect_object(static_cast<const Base&>(
      std::meta::extract<const Derived&>(substitute(^^derived, {std::meta::reflect_constant(v)}))));
}
template <Base b>
constexpr int dispatch = b.f();
static_assert(dispatch<Base{7}> == 2);
static_assert(addr<Base{7}> == &derived<7>);

// An array element.
struct Digit {
  int d;
  consteval operator template() const;
};
struct Table {
  Digit digits[10];
};
constexpr Table table{{{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}}};
consteval Digit::operator template() const { return std::meta::reflect_object(table.digits[d]); }
static_assert(addr<Digit{3}> == &table.digits[3]);
static_assert(type_of(std::meta::reflect_constant(Digit{3})) == ^^const Digit);

template <Year y>
int get_year() {
  return y.y;
}

int main(int, char**) {
  // The same objects at run time.
  if (addr<Year{2024}> != &bundle<2024, 1, 1>.ymd.y)
    return 1;
  if (addr<Base{7}>->f() != 2)
    return 2;
  if (addr<Digit{5}>->d != 5)
    return 3;
  if (get_year<Year{2024}>() != 2024)
    return 4;
  return 0;
}
