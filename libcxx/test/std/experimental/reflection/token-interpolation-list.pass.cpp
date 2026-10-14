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

// A list interpolated into a token sequence: '\(e1, e2, ...; sep)'. The
// elements are a list, as a function's arguments are -- any may be a pack
// expansion -- and each is interpolated as '\(e)' would be, separated by the
// separator's tokens (a ',' when there is none). So '\(a...)' is
// '\(a...[0]), \(a...[1]), ...'. Everything inside '\( )' is the macro's
// language; a '...' outside it is always a token.

#include <meta>
#include <string>
#include <string_view>

using namespace std::meta;

constexpr int sum() { return 0; }
template <class... T> constexpr int sum(int a, T... t) { return a + sum(t...); }
constexpr int count() { return 0; }
template <class... T> constexpr int count(T...) { return sizeof...(T); }

// A pack of raw arguments, comma-separated by default.
__macro sum_of(token_sequence... xs) { return ^^{ sum(\(xs...)) }; }
static_assert(sum_of!(1, 2, 3) == 6);
static_assert(sum_of!() == 0);
static_assert(sum_of!(10) == 10);

// A pack of expression arguments (reflections), as in a variadic macro
// template.
template <class... A> __macro count_of(A&&... a) { return ^^{ count(\(a...)) }; }
static_assert(count_of!(1, 'x', 2.0) == 3);

// Separators: any interpolable value.
__macro all(token_sequence... xs) { return ^^{ (\(xs...; ^^{ && })) }; }
static_assert(all!(true, 1 < 2, sizeof(int) > 0));
static_assert(!all!(true, false));

__macro prod(token_sequence... xs) {
  return ^^{ (1 * \(xs...; punctuator::star)) };
}
static_assert(prod!(2, 3, 4) == 24);

__macro concat(token_sequence... xs) {
  return ^^{ std::string() + \(xs...; ^^{ + std::string("-") + }) };
}

// No separator: statements, the terminator in the pattern.
__macro add_all(token_sequence target, token_sequence... xs) {
  return ^^{ [&] { \(^^{ \(target) += \(xs); }...; ^^{}) }() };
}
constexpr int add_all_test() {
  int n = 0;
  add_all!(n, 1, 2, 3);
  return n;
}
static_assert(add_all_test() == 6);

// A pattern bigger than one interpolation: a nested literal.
template <class... A> __macro twice(A&&... a) {
  return ^^{ sum(\(^^{ 2 * \(a) }...)) };
}
static_assert(twice!(1, 2, 3) == 12);

// Lists without packs, and lists mixing packs and other elements.
__macro pair_list(token_sequence x, token_sequence y) {
  return ^^{ sum(\(x, y)) };
}
static_assert(pair_list!(4, 5) == 9);
__macro joined(token_sequence x, token_sequence y) {
  return ^^{ (\(x, y; ^^{ - })) };
}
static_assert(joined!(10, 3) == 7);
__macro single(token_sequence x) { return ^^{ (\(x; ^^{ - })) }; }
static_assert(single!(10) == 10);
__macro around(token_sequence... xs) {
  return ^^{ sum(\(^^{ 100 }, xs..., ^^{ 1000 })) };
}
static_assert(around!(1, 2) == 1103);
static_assert(around!() == 1100);

// The empty pack after a fixed argument: put the comma in the pattern.
__macro after_first(token_sequence first, token_sequence... rest) {
  return ^^{ count(\(first) \(^^{ , \(rest) }...; ^^{})) };
}
static_assert(after_first!(1) == 1);
static_assert(after_first!(1, 2, 3) == 3);

// In a consteval function template over a pack of reflections.
template <info... Rs> consteval token_sequence ids() {
  return ^^{ \(Rs...; ^^{ + }) };
}
constexpr int i1 = 1, i2 = 20, i3 = 300;
__macro sum_ids() { return ids<^^i1, ^^i2, ^^i3>(); }
static_assert(sum_ids!() == 321);

// A '...' outside the parentheses is a token. Here a fold in the body expands
// 'names', and each element is followed by a literal '...': a pack expansion
// in the generated code.
__macro expand_all(token_sequence... names) {
  list_builder l(^^{ , });
  ((l += ^^{ \(names)... }), ...);
  return ^^{ sum(\(l)) };
}
template <class... A, class... B>
constexpr int expand_all_test(A... as, B... bs) {
  return expand_all!(as, bs);
}
static_assert(expand_all_test<int, int, int>(1, 2, 3) == 6);

// ... as is one after a non-pack interpolation.
__macro forward_pack(token_sequence name) { return ^^{ sum(\(name)...) }; }
template <class... T> constexpr int forward_pack_test(T... ts) {
  return forward_pack!(ts);
}
static_assert(forward_pack_test(1, 2, 3, 4) == 10);

int main() {
  if (concat!(std::string("a"), std::string("b"), std::string("c")) !=
      "a-b-c")
    return 1;
  return 0;
}
