// RUN: %clang_cc1 -std=c++26 -freflection -Wunused-value -verify %s
// Copyright 2026 Jump Trading LLC

// The internal index expression is consumed by the expansion, not discarded.
// In particular, instantiating an array/aggregate expansion must not diagnose
// it as an unused expression at the range's source location.
template <int N> constexpr int arrays() {
  constexpr int values[] = {10, 20};
  int total = 0;
  template for (constexpr int v : values)
    total += v;
  template for (constexpr int v : {1, 2}) {
    template for (constexpr int w : values)
      total += v * w;
  }
  return total;
}
static_assert(arrays<0>() == 120);

template <class T> constexpr int aggregate() {
  constexpr T values{10, 20};
  int total = 0;
  template for (constexpr int v : values)
    total += v;
  return total;
}
struct Pair { int x, y; };
static_assert(aggregate<Pair>() == 30);

// Actual discarded expressions in the body must still be diagnosed.
template <int N> void unused_body() {
  constexpr int values[] = {10, 20};
  template for (constexpr int v : values) {
    N; // expected-warning 6 {{expression result unused}}
  }
}
void instantiate() {
  unused_body<0>(); // expected-note {{in instantiation of function template specialization}}
}
