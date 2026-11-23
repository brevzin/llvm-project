//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection

// <meta>
//
// [meta.reflection.scope] current_function, current_class, current_namespace

#include <meta>

using std::meta::current_class;
using std::meta::current_function;
using std::meta::current_namespace;
using std::meta::info;

static_assert(current_namespace() == ^^::);

                               // =============
                               // free_function
                               // =============

namespace free_function {
static_assert(current_namespace() == ^^free_function);

void fn() {
  static_assert(current_function() == ^^fn);
  static_assert(current_namespace() == ^^free_function);
}

auto deduced() {
  static_assert(current_function() == ^^deduced);
  return 0;
}

// Before the body, the current scope is the namespace.
auto trailing() -> [:is_namespace(current_namespace()) ? ^^int : ^^bool:];
static_assert(type_of(^^trailing) == ^^auto() -> int);

namespace inner {
void fn() {
  static_assert(current_function() == ^^fn);
  static_assert(current_namespace() == ^^free_function::inner);
}
}  // namespace inner

// Lambdas at namespace scope still find the namespace.
constexpr auto lam = [] { return current_namespace(); };
static_assert(lam() == ^^free_function);
}  // namespace free_function

                               // =============
                               // member_access
                               // =============

namespace member_access {
struct S {
  static_assert(current_class() == ^^S);
  static_assert(current_namespace() == ^^member_access);

  void f() {
    static_assert(current_function() == ^^f);
    static_assert(current_class() == ^^S);
    static_assert(current_namespace() == ^^member_access);
  }

  static void g() {
    static_assert(current_function() == ^^g);
    static_assert(current_class() == ^^S);
  }

  void h(this S const&) {
    static_assert(current_function() == ^^h);
    static_assert(current_class() == ^^S);
  }

  struct Nested {
    static_assert(current_class() == ^^Nested);
    void f() { static_assert(current_class() == ^^Nested); }
  };

  static constexpr info in_init = current_class();
};
static_assert(S::in_init == ^^S);

// Local classes: current_namespace walks out through the enclosing function.
void local() {
  struct L {
    static_assert(current_class() == ^^L);
    static_assert(current_namespace() == ^^member_access);
    void f() {
      static_assert(current_function() == ^^f);
      static_assert(current_class() == ^^L);
      static_assert(current_namespace() == ^^member_access);
    }
  };
}
}  // namespace member_access

                                  // =======
                                  // lambdas
                                  // =======

namespace lambdas {
void fn() {
  [] {
    constexpr info f = current_function();
    static_assert(is_function(f) && f != ^^fn);
    // The class of a lambda's call operator is its closure type.
    static_assert(current_class() == parent_of(f));
    static_assert(is_closure_type(current_class()));
    static_assert(current_namespace() == ^^lambdas);
  }();
}
}  // namespace lambdas

                         // ==========================
                         // default_member_initializer
                         // ==========================

namespace default_member_initializer {
struct A {
  int a;
  consteval A(int p) : a(p) {}
};
struct B : A {
  using A::A;
  consteval B(int p, int q) : A(p * q) {}
  info cls = current_class();
  info ns = current_namespace();
};
static_assert(B(1).cls == ^^B);
static_assert(B{1, 2}.cls == ^^B);
static_assert(B(1).ns == ^^default_member_initializer);

struct Agg {
  info ns = current_namespace();
};
namespace inner {
static_assert(Agg{}.ns == ^^default_member_initializer::inner);
}
}  // namespace default_member_initializer

                             // ================
                             // default_argument
                             // ================

namespace default_argument {
consteval info cls_of(info r = current_class()) { return r; }
consteval info fn_of(info r = current_function()) { return r; }
consteval info ns_of(info r = current_namespace()) { return r; }

struct S {
  static_assert(cls_of() == ^^S);
  void f() {
    static_assert(fn_of() == ^^f);
    static_assert(cls_of() == ^^S);
  }
};

namespace inner {
static_assert(ns_of() == ^^default_argument::inner);
}

struct T {
  consteval { static_assert(cls_of() == ^^T); }
};
}  // namespace default_argument

                              // ===============
                              // consteval_block
                              // ===============

// A consteval block is transparent: the current scope is the one it inhabits.
namespace consteval_block {
consteval { static_assert(current_namespace() == ^^consteval_block); }
consteval {
  static_assert(std::meta::access_context::current().scope() ==
                ^^consteval_block);
}

struct S {
  consteval { static_assert(current_class() == ^^S); }
  consteval {
    static_assert(std::meta::access_context::current().scope() == ^^S);
  }
  // Nested consteval blocks, and block scopes, are looked through as well.
  consteval {
    {
      consteval { static_assert(current_class() == ^^S); }
    }
  }
};

void fn() {
  consteval { static_assert(current_function() == ^^fn); }
  // ...but an ordinary lambda is not.
  consteval {
    [] {
      static_assert(is_closure_type(current_class()));
    }();
  }
}

extern "C++" {
static_assert(current_namespace() == ^^consteval_block);
static_assert(std::meta::access_context::current().scope() ==
              ^^consteval_block);
}
}  // namespace consteval_block

                                 // =========
                                 // templates
                                 // =========

namespace templates {
template <class U>
struct C {
  static constexpr info cls = current_class();

  static consteval info f() { return current_function(); }

  consteval bool g() requires (is_type(current_class())) {
    return current_class() == ^^C;
  }
};
static_assert(C<int>::cls == ^^C<int>);
static_assert(C<char>::cls == ^^C<char>);
static_assert(C<int>::f() == ^^C<int>::f);
static_assert(C<int>{}.g());

template <bool B>
consteval int baz() {
  static constexpr int v = current_function() == ^^baz<true> ? 42 : 0;
  return v;
}
static_assert(baz<true>() == 42);
static_assert(baz<false>() == 0);
}  // namespace templates

int main(int, char**) {
  free_function::fn();
  free_function::inner::fn();
  member_access::S{}.f();
  lambdas::fn();
  return 0;
}
