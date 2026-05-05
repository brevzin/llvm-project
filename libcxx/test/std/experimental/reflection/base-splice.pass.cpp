//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection

// Test splicing base specifier reflections

#include <meta>

struct B {
  int x = 42;
};

struct D : B {
  int y = 100;
};

consteval auto get_base_specifier() {
  auto bases = bases_of(^^D, std::meta::access_context::unchecked());
  return bases[0];
}

constexpr int test_direct_base() {
  D d;
  constexpr auto r = get_base_specifier();
  // Access the base subobject via splice
  return d.[:r:].x;
}

static_assert(test_direct_base() == 42);

// Test with multiple inheritance
struct A { int a = 1; };
struct B2 { int b = 2; };
struct C : A, B2 { int c = 3; };

consteval auto get_second_base() {
  auto bases = bases_of(^^C, std::meta::access_context::unchecked());
  return bases[1];  // B2
}

constexpr int test_second_base() {
  C c;
  constexpr auto r = get_second_base();
  return c.[:r:].b;
}

static_assert(test_second_base() == 2);

// Test with arrow operator
constexpr int test_arrow() {
  D d;
  D* pd = &d;
  constexpr auto r = get_base_specifier();
  return pd->[:r:].x;
}

static_assert(test_arrow() == 42);

// Test with deeper inheritance
struct E : D {
  int z = 200;
};

consteval auto get_d_as_base_of_e() {
  auto bases = bases_of(^^E, std::meta::access_context::unchecked());
  return bases[0];  // D (the direct base of E)
}

constexpr int test_deeper_inheritance() {
  E e;
  e.y = 123;
  constexpr auto r = get_d_as_base_of_e();
  return e.[:r:].y;
}

static_assert(test_deeper_inheritance() == 123);

// Test that we can chain accesses through the base
constexpr int test_chain_access() {
  E e;
  constexpr auto r = get_d_as_base_of_e();
  // e.[:r:] gives the D subobject, then .x accesses B::x through D
  return e.[:r:].x;
}

static_assert(test_chain_access() == 42);

int main() {}
