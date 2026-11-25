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

// Functions written with <meta>, compiled to a native plugin
// (-fconsteval-plugin), and called from a translation unit that includes only
// <meta_fwd>: they do there what they would do interpreted, and what cannot
// be done says why.

// RUN: mkdir -p %t
// RUN: %{cxx} %{compile_flags} %{link_flags} -std=c++2d -freflection \
// RUN:     -fconsteval-plugin -fPIC -shared -O2 -DPLUGIN %s -o %t/libplugin.so
// RUN: %{cxx} %{compile_flags} -std=c++2d -freflection \
// RUN:     -fconsteval-plugin-load=%t/libplugin.so -fsyntax-only \
// RUN:     -Xclang -verify %s

#include <string_view>
#ifdef PLUGIN
#  include <meta>
#endif
// (After <meta>, this does nothing: a header shared by a plugin and its users
// can include it.)
#include <meta_fwd>

using std::meta::info;

enum class Color { red, green, blue };
struct Point {
  int x;
  long y;
  static int count;
  void move();
};
template <class T>
concept Small = sizeof(T) <= 4;

consteval auto count_enumerators(info e) -> int;
consteval auto name_of(info r) -> std::string_view;
consteval auto has_member(info cls, std::string_view name) -> bool;
consteval auto member_types_are(info cls, info a, info b) -> bool;
consteval auto decays_to(info t, info d) -> bool;
consteval auto is_small(info t) -> bool;
consteval auto text_of(info t) -> std::string_view;
consteval auto type_of_type(info t) -> info;
consteval auto static_name(info r) -> const char*;
consteval auto getters(info cls) -> void;

#ifdef PLUGIN

consteval auto count_enumerators(info e) -> int {
  return static_cast<int>(enumerators_of(e).size());
}

consteval auto name_of(info r) -> std::string_view { return identifier_of(r); }

consteval auto has_member(info cls, std::string_view name) -> bool {
  constexpr auto ctx = std::meta::access_context::unchecked();
  for (info m : members_of(cls, ctx))
    if (has_identifier(m) && identifier_of(m) == name)
      return true;
  return false;
}

consteval auto member_types_are(info cls, info a, info b) -> bool {
  constexpr auto ctx = std::meta::access_context::unchecked();
  auto members = nonstatic_data_members_of(cls, ctx);
  // (A constant of the enclosing function, used in a lambda.)
  auto type_is = [&](size_t i, info t) {
    return i < nonstatic_data_members_of(cls, ctx).size() &&
           type_of(members[i]) == t;
  };
  return members.size() == 2 && type_is(0, a) && type_is(1, b);
}

consteval auto decays_to(info t, info d) -> bool { return decay(t) == d; }

consteval auto is_small(info t) -> bool {
  return extract<bool>(substitute(^^Small, {t}));
}

consteval auto text_of(info t) -> std::string_view {
  return stringize(^^{ static \(t)* p = nullptr; });
}

consteval auto type_of_type(info t) -> info { return type_of(t); }

consteval auto static_name(info r) -> const char* {
  return std::define_static_string(identifier_of(r));
}

consteval auto getters(info cls) -> void {
  std::meta::list_builder body;
  for (info m : nonstatic_data_members_of(
           cls, std::meta::access_context::unchecked())) {
    auto name = std::meta::id("get_", identifier_of(m));
    body += ^^{
      static constexpr auto \(name)(\(cls) const& o) -> \(type_of(m)) {
        return o.\(m);
      }
    };
  }
  queue_injection(body);
}

#else

// (<meta>'s own iteration works where <meta> is not included.)
static_assert(count_enumerators(^^Color) == 3);

// A string the compiler has is the same string on the way in and out.
static_assert(name_of(^^Point) == "Point");
static_assert(name_of(^^Color::green) == "green");
static_assert(has_member(^^Point, "count") && has_member(^^Point, "move") &&
              !has_member(^^Point, "z"));
static_assert(has_member(^^Point, std::string_view("xyz", 1)));

static_assert(member_types_are(^^Point, ^^int, ^^long));
static_assert(!member_types_are(^^Point, ^^int, ^^int));

// The library's uses of its own templates find them here (<type_traits> is
// included; std::decay_t is there).
static_assert(decays_to(^^const int[3], ^^const int*));
static_assert(is_small(^^int) && !is_small(^^Point));

static_assert(text_of(^^Point) == "static Point* p = nullptr;");

struct Getters {
  consteval { getters(^^Point); }
};
static_assert(Getters::get_x(Point{1, 2}) == 1 &&
              Getters::get_y(Point{1, 2}) == 2);

// A metafunction that fails ends the call, with what it had to say.
constexpr info no_type = type_of_type(^^int);
// expected-error@-1 {{must be initialized by a constant expression}}
// expected-note@-2 {{call to 'type_of_type' in a consteval plugin failed}}
// expected-note@-3 {{a type has no type}}

// What the plugin needs declared and is not, it names.
constexpr const char* name = static_name(^^Point);
// expected-error@-1 {{must be initialized by a constant expression}}
// expected-note@-2 {{call to 'static_name' in a consteval plugin failed: the plugin refers to 'std::meta::__define_static::FixedArray', which is not declared in this translation unit}}

#endif
