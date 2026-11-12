// RUN: %clang_cc1 -std=c++26 -freflection -ast-print %s | FileCheck --strict-whitespace %s

// -ast-print of reflection, splices, token sequences, consteval blocks, and
// expansion statements: what was written, not placeholders.

#include "../Reflection/Inputs/token-sequence.h"

namespace N { int z; }

// CHECK: struct S {
// CHECK-NEXT:     using self = S;
// CHECK-NEXT:     consteval {
// CHECK-NEXT:         (void)^^self;
// CHECK-NEXT:     }
// CHECK-NEXT:     static constexpr auto t = ^^int;
// CHECK-NEXT:     static constexpr auto n = ^^N::z;
// CHECK-NEXT:     static constexpr auto ns = ^^N;
// CHECK-NEXT: };
struct S {
  using self = S;
  consteval { (void)^^self; }
  static constexpr auto t = ^^int;
  static constexpr auto n = ^^N::z;
  static constexpr auto ns = ^^N;
};

// CHECK: int w = [: ^^N::z :];
int w = [:^^N::z:];

// CHECK: template <class T> constexpr int h() {
// CHECK-NEXT:     if constexpr (sizeof(T) > 1)
template <class T> constexpr int h() {
  if constexpr (sizeof(T) > 1) return 1;
  return 2;
}

// CHECK: template <class ...Ts> constexpr int sizes() {
// CHECK-NEXT:     int n = 0;
// CHECK-NEXT:     template for (constexpr auto r : {^^Ts...})
// CHECK-NEXT:         n += sizeof(typename [: r :]);
// CHECK-NEXT:     return n;
// CHECK-NEXT: }
template <class... Ts> constexpr int sizes() {
  int n = 0;
  template for (constexpr auto r : {^^Ts...}) n += sizeof(typename [:r:]);
  return n;
}

// A token literal: spaced, and broken into lines, as written; interpolations
// as '$name' or '${ e }'.
// CHECK: consteval auto make(int k, std::meta::token_sequence name) -> std::meta::token_sequence {
// CHECK-NEXT:     return ^^{
// CHECK-NEXT:       struct $name {
// CHECK-NEXT:         int value = ${ k + 1 };
// CHECK-NEXT:       };
// CHECK-NEXT:     };
// CHECK-NEXT: }
consteval auto make(int k, std::meta::token_sequence name) -> std::meta::token_sequence {
  return ^^{
    struct $name {
      int value = ${ k + 1 };
    };
  };
}

// CHECK: consteval auto one_line() -> std::meta::token_sequence {
// CHECK-NEXT:     return ^^{ a + b(c) };
consteval auto one_line() -> std::meta::token_sequence {
  return ^^{ a + b(c) };
}
