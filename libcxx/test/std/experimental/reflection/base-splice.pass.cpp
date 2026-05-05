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

// Test subobjects_of - returns bases then nonstatic data members
struct F {
  int f1 = 10;
  int f2 = 20;
};

struct G : F {
  int g1 = 30;
  int g2 = 40;
};

consteval auto test_subobjects_of() {
  constexpr auto ctx = std::meta::access_context::unchecked();
  auto subs = subobjects_of(^^G, ctx);

  // Should have 3 subobjects: F (base), g1, g2
  if (subs.size() != 3) return false;

  // First is the base class specifier
  if (!is_base(subs[0])) return false;

  // Next are the data members
  if (!is_nonstatic_data_member(subs[1])) return false;
  if (!is_nonstatic_data_member(subs[2])) return false;

  if (identifier_of(subs[1]) != "g1") return false;
  if (identifier_of(subs[2]) != "g2") return false;

  return true;
}

static_assert(test_subobjects_of());

// Test that subobjects_of works with splicing
consteval auto get_subobject(int idx) {
  return subobjects_of(^^G, std::meta::access_context::unchecked())[idx];
}

constexpr int test_subobjects_splice() {
  G g;

  // Access the base via splice
  constexpr auto base = get_subobject(0);
  int base_sum = g.[:base:].f1 + g.[:base:].f2;

  // Access the data members via splice
  constexpr auto m1 = get_subobject(1);
  constexpr auto m2 = get_subobject(2);
  int member_sum = g.[:m1:] + g.[:m2:];

  return base_sum + member_sum;
}

static_assert(test_subobjects_splice() == 10 + 20 + 30 + 40);

int main() {}
