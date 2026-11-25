// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The token sequences a plugin makes are the ones the same code would make
// when interpreted: a value interpolated into one has its type, a reflection
// stands for what it reflects, names made by id() are checked, and stringize
// gives the same text.
//
// REQUIRES: plugins
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clangxx -std=c++26 -freflection -fconsteval-plugin -fPIC -shared -O2 \
// RUN:   -I%S/Inputs %t/plugin.cpp -o %t/libplugin.so
// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-plugin-load=%t/libplugin.so \
// RUN:   -I%S/Inputs -fsyntax-only -verify=expected,plugin %t/consumer.cpp
//
// The same, interpreted:
// RUN: %clang_cc1 -std=c++26 -freflection -DINTERPRETED \
// RUN:   -I%S/Inputs -fsyntax-only -verify=expected,interp %t/consumer.cpp

//--- decls.h
#include "token-sequence.h"
using info = decltype(^^int);
namespace std::meta {
consteval auto queue_injection(token_sequence tokens) -> void;
consteval auto __stringize(token_sequence) -> char const *;
template <class... Ts> consteval auto str_lit(Ts const &...) -> token_sequence;
} // namespace std::meta

enum class Color { Red = 1, Green = 5 };
namespace ns {
inline constexpr int gv = 7;
template <class T> struct W {
  T t;
};
struct S {
  int field;
};
} // namespace ns

consteval auto values(int i, unsigned long long u, bool b, char c, Color e)
    -> void;
consteval auto reflections(info type, info var, info tmpl, info nsr,
                           info field, info enumerator) -> void;
consteval auto names(int n, char c, unsigned long long big) -> void;
consteval auto bad_name(const char *s) -> void;
consteval auto text(info t) -> const char *;
consteval auto literal(const char *s, int n) -> void;

//--- impl.h
consteval auto values(int i, unsigned long long u, bool b, char c, Color e)
    -> void {
  std::meta::queue_injection(^^{
    static constexpr auto vi = \(i);
    static constexpr auto vu = \(u);
    static constexpr auto vb = \(b);
    static constexpr auto vc = \(c);
    static constexpr auto ve = \(e);
  });
}

consteval auto reflections(info type, info var, info tmpl, info nsr,
                           info field, info enumerator) -> void {
  std::meta::queue_injection(^^{
    \(type) of_type = 1;
    static constexpr auto of_var = \(var);
    \(tmpl)<int> of_tmpl{3};
    static constexpr int of_ns = \(nsr)::gv;
    constexpr int get(ns::S s) const { return s.\(field); }
    static constexpr auto of_enumerator = \(enumerator);
  });
}

consteval auto names(int n, char c, unsigned long long big) -> void {
  auto a = std::meta::id("n_", n);
  auto b = std::meta::id("c_", c);
  auto d = std::meta::id("big_", big);
  std::meta::queue_injection(^^{
    static constexpr int \(a) = 1;
    static constexpr int \(b) = 2;
    static constexpr int \(d) = 3;
  });
}

consteval auto bad_name(const char *s) -> void {
  auto a = std::meta::id(s);
  std::meta::queue_injection(^^{ int \(a); });
}

consteval auto text(info t) -> const char * {
  int n = -4;
  return std::meta::__stringize(^^{ a + b*\(t) [\(n)]; x<y });
}

consteval auto literal(const char *s, int n) -> void {
  auto l = std::meta::str_lit(s, n, "\"q\"");
  std::meta::queue_injection(^^{ static constexpr const char *lit = \(l); });
}

//--- plugin.cpp
#include "decls.h"
#include "impl.h"

//--- consumer.cpp
#include "decls.h"
#ifdef INTERPRETED
#include "impl.h"
#endif

template <class T, class U> constexpr bool same = false;
template <class T> constexpr bool same<T, T> = true;
constexpr bool streq(const char *a, const char *b) {
  while (*a && *a == *b)
    ++a, ++b;
  return *a == *b;
}

// An interpolated value is a constant of its type with its value.
struct V {
  consteval { values(-1, 18446744073709551615ull, true, 'x', Color::Green); }
};
static_assert(V::vi == -1 && same<decltype(V::vi), const int>);
static_assert(V::vu == 18446744073709551615ull &&
              same<decltype(V::vu), const unsigned long long>);
static_assert(V::vb == true && same<decltype(V::vb), const bool>);
static_assert(V::vc == 'x' && same<decltype(V::vc), const char>);
static_assert(V::ve == Color::Green && same<decltype(V::ve), const Color>);

// An interpolated reflection stands for what it reflects.
struct R {
  consteval {
    reflections(^^long, ^^ns::gv, ^^ns::W, ^^ns, ^^ns::S::field, ^^Color::Red);
  }
};
static_assert(same<decltype(R::of_type), long>);
static_assert(R::of_var == 7);
static_assert(R{}.of_tmpl.t == 3 && same<decltype(R::of_tmpl), ns::W<int>>);
static_assert(R::of_ns == 7);
static_assert(R{}.get(ns::S{9}) == 9);
static_assert(R::of_enumerator == Color::Red);

// id() makes the name from its parts as they are: a character is itself.
struct N {
  consteval { names(3, 'k', 18446744073709551615ull); }
};
static_assert(N::n_3 == 1 && N::c_k == 2 && N::big_18446744073709551615 == 3);

// ... and what it makes has to be an identifier.
struct N2 {
  consteval { names(-3, 'k', 0); } // #negative
};
struct N3 {
  consteval { bad_name("not an identifier"); } // #spaces
};
struct N4 {
  consteval { bad_name("while"); } // #keyword
};
// expected-error@#negative {{must be a constant expression}}
// expected-note@#negative {{in call to}}
// expected-error@#spaces {{must be a constant expression}}
// expected-note@#spaces {{in call to}}
// expected-error@#keyword {{must be a constant expression}}
// expected-note@#keyword {{in call to}}
// plugin-note@#negative {{call to 'names' in a consteval plugin failed}}
// plugin-note@#negative {{std::meta::id produces 'n_-3', which is not an identifier}}
// plugin-note@#spaces {{call to 'bad_name' in a consteval plugin failed}}
// plugin-note@#spaces {{std::meta::id produces 'not an identifier', which is not an identifier}}
// plugin-note@#keyword {{call to 'bad_name' in a consteval plugin failed}}
// plugin-note@#keyword {{std::meta::id produces 'while', which is a keyword}}
// interp-note@impl.h:* {{std::meta::id produces 'n_-3', which is not an identifier}}
// interp-note@impl.h:* {{std::meta::id produces 'not an identifier', which is not an identifier}}
// interp-note@impl.h:* {{std::meta::id produces 'while', which is a keyword}}
// interp-note@#negative {{in call to}}
// interp-note@#spaces {{in call to}}
// interp-note@#keyword {{in call to}}

// stringize keeps the spacing of the source, and shows what was interpolated.
constexpr const char *t = text(^^unsigned);
static_assert(streq(t, "a + b*unsigned int [-4]; x<y"));

// str_lit escapes what needs escaping.
struct L {
  consteval { literal("a\\b\n", -2); }
};
static_assert(streq(L::lit, "a\\b\n-2\"q\""));
