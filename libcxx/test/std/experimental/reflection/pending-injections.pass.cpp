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

// RUN: %{build}
// RUN: %{exec} %t.exe

// A class template whose specializations receive members the pattern does
// not have -- from a class-scope macro invocation or a consteval block
// expanded per specialization, or an annotation with an inject_members
// callback (or of a dependent type, which might have one) -- is treated as one
// with a dependent base: a name not found in it, as 'this->x' or 'C::x', is
// looked up again at instantiation instead of being diagnosed. (As with a
// dependent base, an unqualified name does not find such members.)

#include <meta>
#include <cassert>
#include <string_view>

using std::meta::info;
using std::meta::token_sequence;

// ------------------------------------------------ deferred macro ----------

__macro maybe(bool cond, token_sequence decl) {
  if (extract<bool>(constant_of(cond)))
    return decl;
  return ^^{};
}

long sometimes = 1;  // not what 'this->sometimes' finds

template <bool B>
struct C {
  long always;
  maybe!(B, long sometimes);

  long f() { return this->sometimes; }
  long g() { return C::sometimes; }
};

// ----------------------------------------------------- consteval block ----

consteval void add_twice() {
  queue_injection(^^{
    int twice() const { return 2 * this->value; }
  });
}

template <class T>
struct D {
  int value;
  consteval { add_twice(); }
  int four_times() const { return 2 * this->twice(); }
};

// ---------------------------------------------- annotation callbacks ------

struct adds_extra {
  consteval auto inject_members(info) const -> token_sequence {
    return ^^{ int extra = 7; using extra_type = long; };
  }
};

// (inject_members runs right before the class is complete, after the written
// members: its members can be named in member function bodies, which are
// instantiated later, but not in the written members' declarations.)
template <class T>
struct [[=adds_extra{}]] E {
  int get() const { return this->extra; }
  long typed() const {
    typename E::extra_type v = this->extra;
    return v;
  }
};

// A dependent annotation: whether it injects anything is not known until
// instantiation.
template <class T>
struct adds_named {
  consteval auto inject_members(info) const -> token_sequence {
    return ^^{ T named{}; };
  }
};

template <class T>
struct [[=adds_named<T>{}]] F {
  T get() const { return this->named; }
};

// ------------------------------------------------ mem-initializers ------

// A mem-initializer may name a member the class does not have yet; it is
// built at instantiation, in its place among the others. (Which a const or
// reference member, that cannot be assigned in the body, needs.)
consteval void add_members() {
  queue_injection(^^{
    const int fixed;
    int& ref;
  });
}

__macro init_if_member(std::meta::token_sequence init) {
  std::meta::info ctor = std::meta::macro_expansion_context();
  std::string_view name = identifier_of(init[0]);
  for (std::meta::info m : nonstatic_data_members_of(
           parent_of(ctor), std::meta::access_context::unchecked()))
    if (has_identifier(m) && identifier_of(m) == name)
      return init;
  return ^^{};
}

template <class T>
struct G {
  int first;
  consteval { add_members(); }
  maybe!(sizeof(T) > 1, int optional);
  int last;

  G(int& r)
      : first(1), fixed(first + 1), ref(r), init_if_member!(optional(3)),
        last(4) {}
  G(int& r, int);
};

// Out of line, too.
template <class T>
G<T>::G(int& r, int f) : first(f), fixed(f), ref(r), last(f) {}

int main(int, char**) {
  C<true> c{1, 42};
  assert(c.f() == 42 && c.g() == 42);
  static_assert(sizeof(C<false>) == sizeof(long));  // f, g never instantiated

  D<int> d{5};
  assert(d.four_times() == 20);

  E<int> e;
  assert(e.get() == 7 && e.typed() == 7);

  F<short> f;
  assert(f.get() == 0);

  int x = 0;
  G<int> g(x);
  assert(g.first == 1 && g.fixed == 2 && &g.ref == &x && g.optional == 3 &&
         g.last == 4);
  G<char> h(x, 9);
  assert(h.fixed == 9 && h.last == 9);
  // G<char> has no 'optional'.
  static_assert(nonstatic_data_members_of(^^G<char>,
                                          std::meta::access_context::unchecked())
                    .size() == 4);
  static_assert(nonstatic_data_members_of(^^G<int>,
                                          std::meta::access_context::unchecked())
                    .size() == 5);
  return 0;
}
