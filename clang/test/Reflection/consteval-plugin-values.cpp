// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A value passed to or returned from a plugin function arrives as the value it
// was, or the call fails saying which value could not be passed and why.
//
// REQUIRES: plugins
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clangxx -std=c++26 -freflection -fconsteval-plugin -fPIC -shared -O2 \
// RUN:   %t/plugin.cpp -o %t/libplugin.so
// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-plugin-load=%t/libplugin.so \
// RUN:   -fsyntax-only -verify %t/consumer.cpp

//--- decls.h
using info = decltype(^^int);
using size_t = decltype(sizeof 0);
struct Pair {
  int a;
  int b;
};
struct Base {
  short s;
};
struct Empty {};
struct Derived : Base, Empty {
  char c;
  long arr[3];
  Pair p;
  info r;
};
enum class Color : unsigned char { Red = 1, Green = 200 };
// (What a std::string_view is.)
struct View {
  const char *data;
  size_t size;
};
struct Accumulator {
  int total;
  consteval auto plus(int n) const -> int;
  consteval auto add(int n) -> void;
};
union U {
  int i;
  float f;
};
struct Poly {
  virtual void f();
  int x;
};
struct Incomplete;

consteval auto by_value(Pair p) -> int;
consteval auto by_ref(const Pair &p) -> int;
consteval auto by_int_ref(const int &x) -> int;
consteval auto by_rvalue_ref(Pair &&p) -> int;
consteval auto negative(int x, long long y, signed char z) -> long long;
consteval auto color(Color c) -> Color;
consteval auto floats(float f, double d) -> double;
consteval auto derived(const Derived &d) -> Derived;
consteval auto first(View v) -> char;
consteval auto tail(View v) -> View;
consteval auto c_str(const char *s) -> const char *;
consteval auto literal() -> const char *;
consteval auto null() -> const char *;
consteval auto is_null(const char *s, decltype(nullptr)) -> bool;
consteval auto same(const char *a, const char *b) -> bool;

consteval auto mutate(int &x) -> void;
consteval auto mutate_chars(char *p) -> void;
consteval auto take_union(U u) -> int;
consteval auto take_poly(const Poly &p) -> int;
consteval auto take_long_double(long double d) -> int;
consteval auto take_int128(__int128 x) -> int;
consteval auto take_ptr(const int *p) -> int;
consteval auto give_ptr() -> const int *;
consteval auto give_ref() -> const int &;
consteval auto variadic(int n, ...) -> int;
consteval auto throws(int x) -> int;

//--- plugin.cpp
#include "decls.h"

consteval auto by_value(Pair p) -> int { return p.a * 10 + p.b; }
consteval auto by_ref(const Pair &p) -> int { return p.a * 10 + p.b; }
consteval auto by_int_ref(const int &x) -> int { return x * 2; }
consteval auto by_rvalue_ref(Pair &&p) -> int { return p.a - p.b; }
consteval auto negative(int x, long long y, signed char z) -> long long {
  return x < 0 && y < 0 && z < 0 ? x + y + z : 1;
}
consteval auto color(Color c) -> Color {
  return c == Color::Green ? Color::Red : Color::Green;
}
consteval auto floats(float f, double d) -> double { return f + d * 2; }
consteval auto derived(const Derived &d) -> Derived {
  Derived r = d;
  r.s += 1;
  r.c += 1;
  r.arr[2] = d.arr[0] + d.arr[1];
  r.p.b = d.p.a;
  r.r = d.r == ^^int ? ^^long : ^^void;
  return r;
}
consteval auto first(View v) -> char { return v.size ? v.data[0] : '\0'; }
consteval auto tail(View v) -> View { return {v.data + 1, v.size - 1}; }
consteval auto c_str(const char *s) -> const char * { return s + 1; }
consteval auto literal() -> const char * { return "from the plugin"; }
consteval auto null() -> const char * { return nullptr; }
consteval auto is_null(const char *s, decltype(nullptr)) -> bool {
  return s == nullptr;
}
consteval auto same(const char *a, const char *b) -> bool { return a == b; }

consteval auto Accumulator::plus(int n) const -> int { return total + n; }
consteval auto Accumulator::add(int n) -> void { total += n; }

consteval auto mutate(int &x) -> void { x = 1; }
consteval auto mutate_chars(char *p) -> void { *p = 'x'; }
consteval auto take_union(U u) -> int { return u.i; }
consteval auto take_poly(const Poly &p) -> int { return p.x; }
consteval auto take_long_double(long double d) -> int { return d; }
consteval auto take_int128(__int128 x) -> int { return x; }
consteval auto take_ptr(const int *p) -> int { return *p; }
consteval auto give_ptr() -> const int * { return nullptr; }
constexpr int something = 3;
consteval auto give_ref() -> const int & { return something; }
consteval auto variadic(int n, ...) -> int { return n; }
consteval auto throws(int x) -> int {
  if (x < 0)
    throw x;
  return x;
}

//--- consumer.cpp
#include "decls.h"

// A parameter of reference type is given what it refers to.
constexpr Pair pair{3, 4};
constexpr int twenty_one = 21;
static_assert(by_value(pair) == 34);
static_assert(by_ref(pair) == 34);
static_assert(by_ref(Pair{5, 6}) == 56);
static_assert(by_int_ref(twenty_one) == 42);
static_assert(by_int_ref(21) == 42);
static_assert(by_rvalue_ref(Pair{9, 2}) == 7);
consteval int from_local() {
  Pair local{1, 2};
  int n = 5;
  return by_ref(local) + by_int_ref(n);
}
static_assert(from_local() == 22);

// Scalars keep their value, whatever their size and sign.
static_assert(negative(-1, -2, -3) == -6);
static_assert(color(Color::Green) == Color::Red);
static_assert(color(Color::Red) == Color::Green);
static_assert(floats(1.5f, 0.25) == 2.0);

// A class has its bases and members, each where it is.
constexpr Derived d{{7}, {}, 'a', {10, 20, 0}, {1, 2}, ^^int};
constexpr Derived d2 = derived(d);
static_assert(d2.s == 8 && d2.c == 'b' && d2.arr[0] == 10 && d2.arr[1] == 20 &&
              d2.arr[2] == 30 && d2.p.a == 1 && d2.p.b == 1 && d2.r == ^^long);

// A pointer to characters points to the same character of the same array.
constexpr char hello[] = "hello";
constexpr View whole{hello, 5};
static_assert(first(whole) == 'h');
static_assert(first(View{hello + 2, 3}) == 'l');
static_assert(first(View{&"literal"[3], 4}) == 'e');
constexpr View rest = tail(whole);
static_assert(rest.data == hello + 1 && rest.size == 4);
static_assert(c_str(hello) == hello + 1);
static_assert(c_str(hello + 4) == hello + 5);
static_assert(same(hello, hello) && !same(hello, hello + 1));
consteval bool local_chars() {
  char buf[4] = {'x', 'y', 'z', 0};
  return first(View{buf + 1, 2}) == 'y' && c_str(buf) == buf + 1;
}
static_assert(local_chars());
// (A string of the plugin's own is a string here.)
constexpr const char *lit = literal();
static_assert(lit[0] == 'f' && lit[14] == 'n' && lit[15] == '\0');
static_assert(null() == nullptr);
static_assert(is_null(nullptr, nullptr) && !is_null(hello, nullptr));

// A member function is given the object it is called on.
constexpr Accumulator acc{40};
static_assert(acc.plus(2) == 42);
static_assert(Accumulator{1}.plus(2) == 3);

// What cannot be passed is not: the call fails, and says what and why.
consteval int lost_update() {
  int x = 0;
  mutate(x); // expected-note {{call to 'mutate' in a consteval plugin failed: parameter 'x' is a reference to non-const, which cannot be passed to a plugin (what it did to the argument would be lost)}}
  return x;
}
constexpr int e1 = lost_update(); // expected-error {{must be initialized by a constant expression}} expected-note {{in call to}}
consteval int lost_member_update() {
  Accumulator a{0};
  a.add(1); // expected-note {{call to 'add' in a consteval plugin failed: a member function that is not const cannot be called in a plugin (what it did to the object would be lost)}}
  return a.total;
}
constexpr int e2 = lost_member_update(); // expected-error {{must be initialized by a constant expression}} expected-note {{in call to}}
consteval int lost_chars() {
  char buf[2] = {};
  mutate_chars(buf); // expected-note {{call to 'mutate_chars' in a consteval plugin failed: the argument for parameter 'p' cannot be passed to a plugin: 'char *' points to characters the plugin could change, and the change would be lost}}
  return buf[0];
}
constexpr int e3 = lost_chars(); // expected-error {{must be initialized by a constant expression}} expected-note {{in call to}}
constexpr int e4 = take_union(U{1}); // expected-error {{must be initialized by a constant expression}} \
                                     // expected-note {{call to 'take_union' in a consteval plugin failed: the argument for parameter 'u' cannot be passed to a plugin: 'U' is a union}}
constexpr Poly poly{};
constexpr int e5 = take_poly(poly); // expected-error {{must be initialized by a constant expression}} \
                                    // expected-note {{call to 'take_poly' in a consteval plugin failed: the argument for parameter 'p' cannot be passed to a plugin: 'const Poly' has virtual functions or virtual base classes}}
constexpr int e6 = take_long_double(1.0L); // expected-error {{must be initialized by a constant expression}} \
                                           // expected-note {{call to 'take_long_double' in a consteval plugin failed: the argument for parameter 'd' cannot be passed to a plugin: 'long double' is not float or double}}
constexpr int e7 = take_int128(1); // expected-error {{must be initialized by a constant expression}} \
                                   // expected-note {{call to 'take_int128' in a consteval plugin failed: the argument for parameter 'x' cannot be passed to a plugin: '__int128' is not an integer of at most 64 bits}}
constexpr int e8 = take_ptr(&twenty_one); // expected-error {{must be initialized by a constant expression}} \
                                          // expected-note {{call to 'take_ptr' in a consteval plugin failed: the argument for parameter 'p' cannot be passed to a plugin: 'const int *' is a pointer to something other than characters}}
constexpr const int *e9 = give_ptr(); // expected-error {{must be initialized by a constant expression}} \
                                      // expected-note {{call to 'give_ptr' in a consteval plugin failed: the result cannot be returned from a plugin: 'const int *' is a pointer to something other than characters}}
constexpr int e10 = give_ref(); // expected-error {{must be initialized by a constant expression}} \
                                // expected-note {{call to 'give_ref' in a consteval plugin failed: a function returning a reference cannot be called in a plugin}}
constexpr int e11 = variadic(1, 2); // expected-error {{must be initialized by a constant expression}} \
                                    // expected-note {{call to 'variadic' in a consteval plugin failed: a variadic function cannot be called in a plugin}}

// An exception that leaves the plugin's function ends the call; it does not
// reach the compiler.
static_assert(throws(3) == 3);
constexpr int e12 = throws(-1); // expected-error {{must be initialized by a constant expression}} \
                                // expected-note {{call to 'throws' in a consteval plugin failed: an exception was thrown and not caught}}
