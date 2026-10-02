//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// RUN: %{build}
// RUN: %{exec} %t.exe

// std::meta::expand_at_instantiation(): a macro whose arguments are not
// dependent can still ask for its expansion to wait for template
// instantiation. In a templated context the invocation becomes a dependent
// expression and the macro is evaluated again per instantiation (where the
// call does nothing); outside templates the call does nothing at all.

#include <meta>
#include <cassert>
#include <string_view>

// ------------------------------------------------------------------ id! -----

// Names a local by its token, looked up in the instantiated function: it can
// name a local that only an instantiation-time injection declares.
__macro id(std::meta::token_sequence name) {
  std::meta::expand_at_instantiation();
  return name;
}

consteval void declare_v(std::meta::token_sequence init) {
  queue_injection(^^{ auto v = \(init); });
}

template <class T>
int scaled(T t) {
  consteval { declare_v(^^{ t * 10 }); }  // 'v' exists only per instantiation
  return id!(v) + 1;                     // so does the expansion of id!(v)
}

// Outside templates there is nothing to wait for: id!(x) is just x.
int plain() {
  int x = 5;
  return id!(x) * 2;
}

// ------------------------------------------- where the expansion happens ----

// Without expand_at_instantiation(), a macro in a template sees the template
// itself as its expansion context; with it, the specialization.
__macro in_specialization() {
  std::meta::expand_at_instantiation();
  return has_template_arguments(std::meta::macro_expansion_context())
             ? ^^{ true }
             : ^^{ false };
}

__macro in_specialization_eager() {
  return has_template_arguments(std::meta::macro_expansion_context())
             ? ^^{ true }
             : ^^{ false };
}

template <class T>
bool where() {
  return in_specialization!();
}

template <class T>
bool where_eager() {
  return in_specialization_eager!();
}

// --------------------------------------------- deferral is conditional -----

// The call can sit on one path only: this macro waits only when asked to.
__macro maybe_wait(bool wait) {
  if (extract<bool>(constant_of(wait)))
    std::meta::expand_at_instantiation();
  return has_template_arguments(std::meta::macro_expansion_context())
             ? ^^{ 1 }
             : ^^{ 0 };
}

template <class T>
int conditional() {
  return maybe_wait!(true) * 10 + maybe_wait!(false);
}

// -------------------------------------------------------- generic lambda ----

// A generic lambda in an ordinary function is a templated context too: the
// expansion waits for the lambda's call operator to be instantiated.
int generic_lambda() {
  auto f = [](auto t) {
    consteval { declare_v(^^{ t + 100 }); }
    return id!(v);
  };
  return f(1);
}

// ------------------------------------------------ class template members ----

template <class T>
struct Holder {
  T value;
  int get() const {
    consteval { declare_v(^^{ value * 2 }); }
    return id!(v);
  }
};

// ----------------------------------------------------------- member macro ---

struct Counter {
  int n;
  // A member macro that waits as well.
  __macro twice(this Counter const& self) {
    std::meta::expand_at_instantiation();
    return ^^{ (\(self).n * 2) };
  }
};

template <class T>
int member_macro(T) {
  Counter c{21};
  return c.twice!();  // object and arguments not dependent; still deferred
}

int main(int, char**) {
  assert(scaled(4) == 41);
  assert(scaled(2.5) == 26);
  assert(plain() == 10);

  assert(where<int>());
  assert(!where_eager<int>());

  assert(conditional<int>() == 10);

  assert(generic_lambda() == 101);

  assert(Holder<int>{7}.get() == 14);

  assert(member_macro(0) == 42);
  return 0;
}
