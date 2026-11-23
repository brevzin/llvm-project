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
// ADDITIONAL_COMPILE_FLAGS: -std=c++29 -freflection

// P4340/P4341 ext: what normalizing customized subobjects while the
// evaluation is live does not paper over.

#include <meta>
#include <string>

constexpr const char* Long = "a string too long for the small buffer";

// A transient allocation outside of any customized subobject is still not a
// constant template argument.
struct raw {
  std::string s;
  int* p;
};

[[= raw{Long, new int(1)}]] int a;
// expected-error@-1 {{C++26 annotation attribute requires an expression usable as a template argument}}
// expected-note@-2 {{subexpression not valid in a constant expression}}
// expected-note@-3 {{provided value cannot be represented by a reflection}}
// expected-note@-4 {{pointer to heap-allocated object is not a constant expression}}

template <raw R>
struct tag {};
tag<raw{Long, new int(1)}> t;
// expected-error@-1 {{non-type template argument is not a constant expression}}
// expected-note@-2 {{subexpression not valid in a constant expression}}
// expected-note@-3 {{provided value cannot be represented by a reflection}}
// expected-note@-4 {{pointer to heap-allocated object is not a constant expression}}
// expected-note@-7 {{template parameter is declared here}}

// The value still has to be a constant expression.
std::string runtime();
struct option {
  std::string help;
};
[[= option{runtime()}]] int b;
// expected-error@-1 {{C++26 annotation attribute requires an expression usable as a template argument}}
// expected-note@-2 {{non-constexpr function 'runtime' cannot be used in a constant expression}}
// expected-note@-7 {{declared here}}

// A subobject's customization is held to the same rules as a top-level one.
struct bad {
  int v;
  consteval operator template() const { return ^^int; }
};
struct holds_bad {
  bad b;
  std::string s;
};
[[= holds_bad{{1}, Long}]] int c;
// expected-error@-1 {{'operator template' for 'bad' returned a reflection of something other than an object or variable}}
// expected-error@-2 {{C++26 annotation attribute requires an expression usable as a template argument}}
// expected-note@-3 {{subexpression not valid in a constant expression}}
