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

// A token literal ^^{ ... } is a std::meta::token_sequence: a value holding
// tokens the compiler owns (static, like a string literal's). Copying one is
// cheap, += and + make a longer one and subseq a shorter one, and none of
// these disturbs any other sequence. Its elements are read-only.

#include <meta>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

using std::meta::token;
using std::meta::token_sequence;

// ------------------------------------------------------------ the type -----

static_assert(std::is_same_v<decltype(^^{ a }), token_sequence>);
static_assert(std::is_trivially_copyable_v<token_sequence>);
static_assert(std::is_default_constructible_v<token_sequence>);
static_assert(std::ranges::contiguous_range<token_sequence>);
static_assert(std::ranges::borrowed_range<token_sequence>);
static_assert(std::ranges::view<token_sequence>);
static_assert(std::is_same_v<std::ranges::range_reference_t<token_sequence>,
                             const token&>);

// Static storage: a constexpr token_sequence needs no persistent allocation.
constexpr token_sequence stored = ^^{ int k; };
static_assert(stored.size() == 3 && stored[1] == ^^{ k });

// ------------------------------------------------------- reading it --------

consteval bool reading() {
  token_sequence v = ^^{ a + b };
  int idents = 0;
  for (token t : v)
    idents += token_kind_of(t) == std::meta::token_kind::identifier;
  return v.size() == 3 && !v.empty() && v.front() == ^^{ a } &&
         v.back() == ^^{ b } && *(v.begin() + 1) == ^^{ + } && idents == 2;
}
static_assert(reading());

// ------------------------------------------------------- building ----------

consteval bool building() {
  token_sequence e;
  token_sequence s = ^^{ a };
  s += ^^{ + b };                   // a sequence
  s += std::meta::id("c");          // a token
  token_sequence t = ^^{ p } + s;   // concatenation
  token_sequence u = std::meta::id("x") + ^^{ y } + std::meta::id("z");
  return e.empty() && s == ^^{ a + b c } && t == ^^{ p a + b c } &&
         u == ^^{ x y z };
}
static_assert(building());

// From tokens: listed, a contiguous range, any other range.
consteval bool constructing() {
  token_sequence s = ^^{ f ( 1 , 2 ) };
  token_sequence listed{s[0], s[1], s[5]};
  token_sequence spanned(std::span<const token>(s.data() + 2, 3));
  token_sequence filtered(
      s | std::views::filter([](token t) { return !(t == ^^{ , }); }));
  std::vector<token> v(s.begin(), s.end());
  token_sequence from_vector(v);
  return listed == ^^{ f ( ) } && spanned == ^^{ 1 , 2 } &&
         filtered == ^^{ f ( 1 2 ) } && from_vector == s;
}
static_assert(constructing());

// ------------------------------------------------------- value semantics ---

consteval bool values() {
  token_sequence a = ^^{ p };
  a += ^^{ q };
  token_sequence b = a, c = a;
  b += ^^{ r };  // two copies appending from the same end
  c += ^^{ s };
  c += c;        // a sequence appended to itself
  return a == ^^{ p q } && b == ^^{ p q r } && c == ^^{ p q s p q s };
}
static_assert(values());

// Building one token at a time keeps every earlier copy intact.
consteval bool snapshots() {
  token_sequence s;
  std::vector<token_sequence> history;
  for (token t : ^^{ a b c d e }) {
    s += t;
    history.push_back(s);
  }
  return history[0] == ^^{ a } && history[2] == ^^{ a b c } &&
         history[4] == ^^{ a b c d e } && s.size() == 5;
}
static_assert(snapshots());

// ------------------------------------------------------- subseq ------------

consteval bool slicing() {
  token_sequence s = ^^{ f ( 1 , 2 ) };
  return s.subseq(2, 3) == ^^{ 1 , 2 } && s.subseq(4) == ^^{ 2 ) } &&
         s.subseq(6).empty() && s.subseq(0, 100) == s &&
         s.subseq(2).subseq(1, 1) == ^^{ , } && s == ^^{ f ( 1 , 2 ) };
}
static_assert(slicing());

// A slice is a sequence like any other: appending to one copies it.
consteval bool slice_then_append() {
  token_sequence s = ^^{ a b c };
  token_sequence head = s.subseq(0, 2);
  head += ^^{ x };
  return head == ^^{ a b x } && s == ^^{ a b c };
}
static_assert(slice_then_append());

// Splitting at top-level commas, by slicing.
consteval auto split(token_sequence s) -> std::vector<token_sequence> {
  std::vector<token_sequence> out;
  size_t start = 0;
  int depth = 0;
  for (size_t i = 0; i != s.size(); ++i) {
    if (s[i] == ^^{ ( })
      ++depth;
    else if (s[i] == ^^{ ) })
      --depth;
    else if (depth == 0 && s[i] == ^^{ , }) {
      out.push_back(s.subseq(start, i - start));
      start = i + 1;
    }
  }
  out.push_back(s.subseq(start));
  return out;
}
consteval bool splitting() {
  auto parts = split(^^{ a , f ( b , c ) , d });
  return parts.size() == 3 && parts[0] == ^^{ a } &&
         parts[1] == ^^{ f ( b , c ) } && parts[2] == ^^{ d };
}
static_assert(splitting());

// ------------------------------------------------------- interpolation -----

consteval auto wrap(std::span<const token> t) -> token_sequence {
  return ^^{ (\(t)) };
}
consteval bool interpolation() {
  token_sequence s = ^^{ 1 + 2 };
  return wrap(^^{ a }) == ^^{ (a) } && wrap(s) == ^^{ (1 + 2) } &&
         ^^{ \(^^{ x }) \(s) \(s.subseq(1)) } == ^^{ x 1 + 2 + 2 };
}
static_assert(interpolation());

// Reading a constexpr token_sequence does not odr-use it: a consteval block
// or a lambda can interpolate one from the enclosing function without
// capturing it.
constexpr int read_local() {
  constexpr token_sequence seven = ^^{ 7 };
  consteval { queue_injection(^^{ constexpr int from_block = \(seven); }); }
  return [] { return int(token_sequence(^^{ \(seven) }).size()); }() +
         from_block;
}
static_assert(read_local() == 8);

// Literals that compare equal but differ (interpolated constants of different
// types) have their own storage.
enum class E0 { e };
enum class E1 { e };
consteval auto case_of(std::meta::info e) -> token_sequence {
  return ^^{ \(e) };
}
template <class E>
consteval bool same_enum() {
  token_sequence v = case_of(enumerators_of(^^E)[0]);
  queue_injection(^^{ constexpr auto \(std::meta::id(identifier_of(^^E), "_value")) = \(v); });
  return true;
}
consteval {
  same_enum<E0>();
  same_enum<E1>();
}
static_assert(std::is_same_v<decltype(E0_value), const E0>);
static_assert(std::is_same_v<decltype(E1_value), const E1>);

// ------------------------------------------------------- raw parameters ----

// The same object every time it is mentioned.
__macro count(token_sequence names) {
  int n = 0;
  for (auto b = names.begin(), e = names.end(); b != e; ++b)
    ++n;
  return ^^{ \(std::meta::reflect_constant(n)) };
}
static_assert(count!(a b c) == 3);

// Modified: the parameter is the macro's own copy.
__macro plus_one(token_sequence names) {
  names += std::meta::id("z");
  return ^^{ \(std::meta::reflect_constant(int(names.size()))) };
}
static_assert(plus_one!(a b) == 3);

// A pack of them.
__macro sizes(token_sequence... args) {
  int total = 0;
  ((total = total * 10 + int(args.size())), ...);
  return ^^{ \(std::meta::reflect_constant(total)) };
}
static_assert(sizes!(a, b c, d e f) == 123);

// --------------------------------------------------------------- stringize ----

static_assert(std::is_same_v<decltype(std::meta::stringize(^^{ a })),
                             std::string_view>);
static_assert(std::meta::stringize(^^{ a  +  b }) == "a + b");
static_assert(std::meta::stringize(^^{ }).empty());
static_assert(std::meta::stringize((^^{ x += 1 })[1]) == "+=");

int main(int, char**) { return 0; }
