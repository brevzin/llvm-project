// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A reflect-expression in a plugin is the reflection of the same thing in the
// translation unit using the plugin -- whatever kind of thing it is -- and
// reflections compare there as they do everywhere else. A constant holding a
// reflection can be used wherever the plugin's source has it.
//
// REQUIRES: plugins
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clangxx -std=c++26 -freflection -fconsteval-plugin -fPIC -shared -O2 \
// RUN:   %t/plugin.cpp -o %t/libplugin.so
// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-plugin-load=%t/libplugin.so \
// RUN:   -fsyntax-only -verify %t/consumer.cpp
// RUN: not %clangxx -std=c++26 -freflection -fconsteval-plugin -fPIC -shared -O2 \
// RUN:   %t/local.cpp -o %t/liblocal.so 2>&1 | FileCheck %s --check-prefix=LOCAL

//--- decls.h
using info = decltype(^^int);
namespace ns {
struct X {
  int mem;
  static int smem;
  void fn(int) const;
  void fn(double);
  struct Inner {};
  enum E { A, B };
  X operator+(const X &) const;
  operator int() const;
};
enum class Scoped { One, Two };
void fn();
void over(int);
void over(long);
inline int var = 0;
template <class T, int N = 3> struct Tmpl {
  T t;
  static int smem;
};
template <class T> T vtmpl = T();
template <class T> void ftmpl(T);
template <class T> using atmpl = Tmpl<T, 7>;
template <class T> concept C = true;
inline namespace inl {
struct InInline {};
} // namespace inl
namespace nested {}
} // namespace ns
namespace nsalias = ns;
using IntAlias = int;
int operator""_lit(unsigned long long);
struct Cfg {
  info a;
  int n;
};

#define FOR_EACH(X)                                                            \
  X(void) X(bool) X(char) X(signed char) X(unsigned char) X(short)             \
  X(unsigned short) X(int) X(unsigned) X(long) X(unsigned long long)           \
  X(float) X(double) X(long double) X(wchar_t) X(char8_t) X(char16_t)          \
  X(char32_t) X(decltype(nullptr)) X(info)                                     \
  X(const int) X(volatile int) X(const volatile int) X(int *) X(const int *)   \
  X(int *const) X(int &) X(const int &) X(int &&) X(int[3]) X(const int[3])    \
  X(int[]) X(int[2][3]) X(void()) X(void(int, ...)) X(int(double) noexcept)    \
  X(void() const &) X(int (*)(char)) X(int ns::X::*)                           \
  X(void (ns::X::*)(int) const)                                                \
  X(ns::X) X(const ns::X) X(ns::X *) X(ns::X &) X(ns::X::Inner) X(ns::X::E)    \
  X(ns::Scoped) X(ns::InInline) X(ns::Tmpl<int>) X(ns::Tmpl<ns::X, 5>)         \
  X(const ns::Tmpl<ns::Tmpl<char>> *) X(ns::Tmpl<int(ns::X)>)                  \
  X(IntAlias) X(ns::atmpl<int>)                                                \
  X(ns::Tmpl) X(ns::vtmpl) X(ns::ftmpl) X(ns::atmpl) X(ns::C)                  \
  X(ns::vtmpl<int>) X(ns::ftmpl<int>) X(ns::ftmpl<ns::X>)                      \
  X(ns::Tmpl<int>::t) X(ns::Tmpl<long>::smem)                                  \
  X(ns) X(ns::nested) X(nsalias) X(::)                                         \
  X(ns::fn) X(ns::var) X(ns::X::mem) X(ns::X::smem) X(ns::X::A)                \
  X(ns::Scoped::Two) X(ns::X::operator+) X(ns::X::operator int)                \
  X(operator""_lit)

//--- plugin.cpp
#include "decls.h"

// Each reflection the list names, in order.
consteval auto nth(int n) -> info {
  int i = 0;
#define X(...)                                                                 \
  if (i++ == n)                                                                \
    return ^^__VA_ARGS__;
  FOR_EACH(X)
#undef X
  return info();
}

// Which of them 'r' is (and it must be exactly one).
consteval auto index_of(info r) -> int {
  int i = 0, found = -1, count = 0;
#define X(...)                                                                 \
  if (r == ^^__VA_ARGS__) {                                                    \
    found = i;                                                                 \
    ++count;                                                                   \
  }                                                                            \
  ++i;
  FOR_EACH(X)
#undef X
  return count == 1 ? found : -1 - count;
}

consteval auto alias_is_not_its_type() -> bool {
  bool typedef_differs = ^^IntAlias != ^^int;
  bool alias_template_differs = ^^ns::atmpl<int> != ^^ns::Tmpl<int, 7>;
  // (A cv-qualified alias is not an alias.)
  bool qualified_is_the_type = ^^const IntAlias == ^^const int;
  return typedef_differs && alias_template_differs && qualified_is_the_type;
}

// Constants holding reflections.
constexpr info g_int = ^^int;
constexpr info g_types[] = {^^int, ^^double};
constexpr Cfg g_cfg{^^long, 3};
struct Statics {
  static constexpr info member = ^^ns::X;
};

consteval auto uses_global(info t) -> bool { return t == g_int; }
consteval auto uses_array(info t) -> bool { return t == g_types[1]; }
consteval auto uses_member(info t) -> bool { return t == Statics::member; }
consteval auto uses_local(info t) -> bool {
  constexpr info l = ^^char;
  return t == l;
}
consteval auto uses_static_local(info t) -> bool {
  static constexpr info l = ^^short;
  return t == l;
}
consteval auto uses_aggregate(info t) -> bool {
  Cfg c = g_cfg;
  const Cfg &r = g_cfg;
  return t == c.a && c.n == 3 && g_cfg.a == t && r.a == t;
}
consteval auto uses_in_lambda(info t) -> bool {
  constexpr info want = ^^unsigned;
  constexpr Cfg cfg{^^float, 1};
  auto f = [](info x) { return x == want && cfg.a == ^^float; };
  return f(t);
}
consteval auto loops(info t) -> int {
  int n = 0;
  for (info x : g_types)
    n += x == t;
  return n;
}

//--- consumer.cpp
// expected-no-diagnostics
#include "decls.h"

consteval auto nth(int n) -> info;
consteval auto index_of(info r) -> int;
consteval auto alias_is_not_its_type() -> bool;

constexpr info all[] = {
#define X(...) ^^__VA_ARGS__,
    FOR_EACH(X)
#undef X
};

consteval bool check() {
  int i = 0;
  for (info r : all) {
    // The plugin's ^^X is this translation unit's ^^X ...
    if (nth(i) != r)
      return false;
    // ... and == in the plugin says what == here does.
    if (index_of(r) != i)
      return false;
    ++i;
  }
  return nth(i) == info() && index_of(^^Cfg) == -1;
}
static_assert(check());
static_assert(alias_is_not_its_type());

consteval auto uses_global(info t) -> bool;
consteval auto uses_array(info t) -> bool;
consteval auto uses_member(info t) -> bool;
consteval auto uses_local(info t) -> bool;
consteval auto uses_static_local(info t) -> bool;
consteval auto uses_aggregate(info t) -> bool;
consteval auto uses_in_lambda(info t) -> bool;
consteval auto loops(info t) -> int;

static_assert(uses_global(^^int) && !uses_global(^^long));
static_assert(uses_array(^^double) && !uses_array(^^int));
static_assert(uses_member(^^ns::X));
static_assert(uses_local(^^char));
static_assert(uses_static_local(^^short));
static_assert(uses_static_local(^^short));
static_assert(uses_aggregate(^^long));
static_assert(uses_in_lambda(^^unsigned) && !uses_in_lambda(^^int));
static_assert(loops(^^double) == 1 && loops(^^char) == 0);

//--- local.cpp
// What the consumer could not name cannot be reflected for it.
using info = decltype(^^int);
namespace {
struct Hidden {};
} // namespace
consteval auto f() -> info {
  struct Local {};
  using LocalAlias = int;
  // LOCAL: local.cpp:{{.*}} error: this reflection cannot be used in a consteval plugin: 'Local' is local to a function
  info a = ^^Local;
  // LOCAL: local.cpp:{{.*}} error: this reflection cannot be used in a consteval plugin: 'LocalAlias' is local to a function
  info b = ^^LocalAlias;
  // LOCAL: local.cpp:{{.*}} error: this reflection cannot be used in a consteval plugin: '(anonymous namespace)::Hidden' is declared in an unnamed namespace
  info c = ^^Hidden;
  return a == b ? c : a;
}
