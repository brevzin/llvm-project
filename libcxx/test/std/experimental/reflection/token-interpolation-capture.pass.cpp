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

// An interpolated expression in a token sequence is a potentially-evaluated
// use, as it was when parsed: inside a lambda, a local used only in an
// interpolation is implicitly captured -- also once a template containing
// the lambda (here, a consteval block of a class template) is instantiated.

#include <meta>
#include <tuple>

using namespace std::meta;

template <class T> struct ByRef {
  consteval {
    int i = 7;
    auto inject = [&] { queue_injection(^^{ static constexpr int v = $i; }); };
    inject();
  }
};
static_assert(ByRef<int>::v == 7);

template <class T> struct ByCopy {
  consteval {
    int i = 8;
    auto inject = [=] { queue_injection(^^{ static constexpr int v = \(i); }); };
    inject();
  }
};
static_assert(ByCopy<int>::v == 8);

// A helper lambda called once per element, interpolating a counter it
// captures by reference (in a template argument list).
template <class... Ts> struct Indexed {
  std::tuple<Ts...> t;
  consteval {
    std::size_t i = 0;
    auto add = [&](token_sequence name) {
      queue_injection(^^{
        constexpr decltype(auto) \(name)() const { return std::get<$i>(t); }
      });
    };
    for (token_sequence name : {^^{ first }, ^^{ second }})
      if (i < sizeof...(Ts)) {
        add(name);
        ++i;
      }
  }
};
constexpr Indexed<int, char> idx{{1, 'x'}};
static_assert(idx.first() == 1 && idx.second() == 'x');

int main(int, char**) { return 0; }
