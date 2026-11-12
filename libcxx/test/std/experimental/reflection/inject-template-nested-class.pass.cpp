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

// A member template injected into a class nested in a class template
// specialization is parsed there, concretely: its template parameters are at
// depth 0, and the enclosing specialization's arguments are not an outer
// level of them (A=int must not be substituted for T).

#include <meta>
#include <type_traits>

template <class A>
struct Outer {
  struct Inner {
    consteval {
      queue_injection(^^{
        template <class T> static constexpr bool is_char = std::is_same_v<T, char>;
        template <class T> static constexpr auto id(T t) -> T { return t; }
        template <class T> static constexpr auto sized() -> std::size_t { return sizeof(T); }
        template <class T> using ptr = T*;
        template <class T> struct box { T value; };
      });
    }
  };

  // and one more level down
  template <class B>
  struct Deeper {
    struct Innermost {
      consteval {
        queue_injection(^^{
          template <class T> static constexpr bool is_char = std::is_same_v<T, char>;
        });
      }
    };
  };
};

// directly into the specialization, too
template <class A>
struct Direct {
  consteval {
    queue_injection(^^{ template <class T> struct box { T value; }; });
  }
};
static_assert(std::is_same_v<decltype(Direct<int>::box<char>::value), char>);

using I = Outer<int>::Inner;
static_assert(I::is_char<char>);
static_assert(!I::is_char<int>);
static_assert(std::is_same_v<decltype(I::id(1.0)), double>);
static_assert(I::sized<char>() == 1);
static_assert(std::is_same_v<I::ptr<char>, char*>);
static_assert(std::is_same_v<decltype(I::box<char>::value), char>);

static_assert(Outer<int>::Deeper<long>::Innermost::is_char<char>);
static_assert(!Outer<int>::Deeper<long>::Innermost::is_char<int>);
static_assert(!Outer<int>::Deeper<long>::Innermost::is_char<long>);

int main(int, char**) { return 0; }
