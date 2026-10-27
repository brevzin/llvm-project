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

// C++26 trivial unions (P3074R7, CWG3189) through reflection: a union defined
// by define_aggregate or injected as tokens gets the same special members as
// one declared directly, and the type-trait metafunctions agree.

#include <meta>
#include <string>

using namespace std::meta;

// Declared directly.
union Direct {
  std::string s;
};
static_assert(is_trivially_default_constructible_type(^^Direct));
static_assert(is_trivially_destructible_type(^^Direct));
static_assert(is_destructible_type(^^Direct));
static_assert(!is_copy_constructible_type(^^Direct));

// Defined by define_aggregate.
union Defined;
consteval {
  define_aggregate(^^Defined, {data_member_spec(^^std::string, {.name = "s"})});
}
static_assert(is_trivially_default_constructible_type(^^Defined));
static_assert(is_trivially_destructible_type(^^Defined));
static_assert(!is_copy_constructible_type(^^Defined));

// With a default member initializer (not expressible through define_aggregate),
// the destructor is deleted.
union WithInit {
  std::string s = "x";
};
static_assert(!is_destructible_type(^^WithInit));
static_assert(!is_trivially_default_constructible_type(^^WithInit));

// Injected as tokens.
struct Holder {
  consteval { queue_injection(^^{ union { std::string s; }; }); }
};
static_assert(is_trivially_destructible_type(^^Holder));
static_assert(is_trivially_default_constructible_type(^^Holder));

int main(int, char**) { return 0; }
