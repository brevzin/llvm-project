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

// A generator's interpolation into a literal nested in its own ('\\(x)' in a
// generated macro's body) comes out the same when the generated macro runs:
// a reflection of a function, variable, or member still names it -- not its
// value -- and survives the instantiation of the (templated) macro.

#include <meta>

using namespace std::meta;

struct Src {
  int field = 3;
  constexpr int member(int x) const { return x + 1; }
};
constexpr int free_fn(int x) { return x * 2; }
int global = 5; // not a constant: interpolated as itself, never read here

struct Gen {
  Src src;
  consteval {
    info member = ^^Src::member;
    info fn = ^^free_fn;
    info var = ^^global;
    info field = ^^Src::field;
    info type = ^^long;
    int k = 4;
    queue_injection(^^{
      template <class Self> __macro call_member(this Self&& self, int x) {
        return ^^{ $self.src.\\(member)(\(x)) };
      }
      template <class Self> __macro call_free(this Self&&, int x) {
        return ^^{ \\(fn)(\(x)) };
      }
      template <class Self> __macro address_of_global(this Self&&) {
        return ^^{ &\\(var) };
      }
      template <class Self> __macro read_field(this Self&& self) {
        return ^^{ $self.src.\\(field) };
      }
      template <class Self> __macro make(this Self&&, int x) {
        return ^^{ \\(type)(\(x)) + \\(k) };
      }
    });
  }
};

constexpr Gen gen{};
static_assert(gen.call_member!(5) == 6);
static_assert(gen.call_free!(5) == 10);
static_assert(gen.address_of_global!() == &global);
static_assert(gen.read_field!() == 3);
static_assert(gen.make!(1) == 5L);

int main(int, char**) { return 0; }
