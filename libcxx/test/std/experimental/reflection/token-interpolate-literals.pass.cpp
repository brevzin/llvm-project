//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// An interpolated value of integral type is a literal of that type: a bool is
// 'true' or 'false', a character a character literal of its kind.

#include <meta>
#include <type_traits>

enum class E : short { a = 7 };

consteval auto make() -> std::meta::token_sequence {
  bool b = true, f = false;
  char c = 'w', n = '\n';
  wchar_t w = L'v';
  char8_t c8 = u8'x';
  char16_t c16 = u'y';
  char32_t c32 = U'z';
  unsigned long ul = 42;
  E e = E::a;
  return ^^{
    static constexpr auto B = $b;
    static constexpr auto F = $f;
    static constexpr auto C = $c;
    static constexpr auto N = $n;
    static constexpr auto W = $w;
    static constexpr auto C8 = $c8;
    static constexpr auto C16 = $c16;
    static constexpr auto C32 = $c32;
    static constexpr auto UL = $ul;
    static constexpr auto EE = $e;
  };
}

struct S {
  consteval { queue_injection(make()); }
};

template <class T, auto V>
constexpr bool is = std::is_same_v<decltype(V), T>;

static_assert(S::B == true && is<bool, S::B>);
static_assert(S::F == false && is<bool, S::F>);
static_assert(S::C == 'w' && is<char, S::C>);
static_assert(S::N == '\n' && is<char, S::N>);
static_assert(S::W == L'v' && is<wchar_t, S::W>);
static_assert(S::C8 == u8'x' && is<char8_t, S::C8>);
static_assert(S::C16 == u'y' && is<char16_t, S::C16>);
static_assert(S::C32 == U'z' && is<char32_t, S::C32>);
static_assert(S::UL == 42 && is<unsigned long, S::UL>);
static_assert(S::EE == E::a && is<E, S::EE>);

int main(int, char**) { return 0; }
