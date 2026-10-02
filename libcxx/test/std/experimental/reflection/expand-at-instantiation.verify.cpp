//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// std::meta::expand_at_instantiation() outside a macro body, and in a macro
// invoked where the expansion cannot (yet) wait for instantiation.

#include <meta>

// Not while expanding a macro.
consteval bool outside() {
  std::meta::expand_at_instantiation(); // #call
  return true;
}
constexpr bool b = outside(); // #b
// expected-error@#b {{constexpr variable 'b' must be initialized by a constant expression}}
// expected-note@*:* {{subexpression not valid in a constant expression}}
// expected-note@#call {{in call to 'expand_at_instantiation()'}}
// expected-note@#call {{std::meta::expand_at_instantiation() is only available while expanding a macro}}
// expected-note@#b {{in call to 'outside()'}}

// An operator macro has no deferred form of its own (yet).
struct flag {
  bool on;
};
template <class R>
__macro operator&&(flag const& l, R&& r) {
  std::meta::expand_at_instantiation();
  return ^^{ (\(l).on && static_cast<bool>(\(r))) };
}

template <class T>
bool use_operator() {
  flag f{true};
  return f && true; // expected-error {{macro 'operator&&<bool>' cannot wait for template instantiation in this position}}
}
