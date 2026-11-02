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

// Querying a part of a declaration that isn't known yet is not a constant
// expression; it doesn't give an answer that changes later. See
// is-complete-declaration.pass.cpp.

#include <meta>

using std::meta::access_context;
using std::meta::info;

consteval auto first_member(info ty) -> info {
  return members_of(ty, access_context::unchecked())[0];
}

struct X {
  X() = default;
  auto f() { return 1; }
  void g(int a = sizeof(X)) noexcept(sizeof(X) > 0);

  static constexpr auto t1 = type_of(^^X::f);
  // expected-error@-1 {{constexpr variable 't1' must be initialized by a constant expression}}
  // expected-note@-2 {{cannot yet query the type of 'f': its return type has not been deduced}}

  static constexpr auto t2 = type_of(^^X::g);
  // expected-error@-1 {{constexpr variable 't2' must be initialized by a constant expression}}
  // expected-note@-2 {{cannot yet query the type of 'g': its noexcept-specifier has not been parsed}}

  static constexpr auto r1 = return_type_of(^^X::f);
  // expected-error@-1 {{constexpr variable 'r1' must be initialized by a constant expression}}
  // expected-note@-2 {{cannot query the return type of function 'f', which has not been deduced yet}}

  static constexpr auto n1 = is_noexcept(^^X::g);
  // expected-error@-1 {{constexpr variable 'n1' must be initialized by a constant expression}}
  // expected-note@-2 {{cannot yet query whether 'g' is noexcept: its noexcept-specifier has not been parsed}}

  static constexpr auto n2 = is_noexcept(first_member(^^X));
  // expected-error@-1 {{constexpr variable 'n2' must be initialized by a constant expression}}
  // expected-note@-2 {{cannot yet query whether 'X' is noexcept: it is defaulted in a class that is not yet complete}}

  static constexpr auto d1 = is_deleted(first_member(^^X));
  // expected-error@-1 {{constexpr variable 'd1' must be initialized by a constant expression}}
  // expected-note@-2 {{cannot yet query whether 'X' is deleted: it is defaulted in a class that is not yet complete}}

  static constexpr auto a1 =
      std::meta::default_argument_of(parameters_of(^^X::g)[0]);
  // expected-error@-2 {{constexpr variable 'a1' must be initialized by a constant expression}}
  // expected-note@-2 {{cannot yet query the default argument of 'a': its default argument has not been parsed}}

  static constexpr auto e1 = extract<int (X::*)()>(^^X::f);
  // expected-error@-1 {{constexpr variable 'e1' must be initialized by a constant expression}}
  // expected-note@-2 {{cannot yet query the address of 'f': its return type has not been deduced}}
};

namespace ns {
auto later();
constexpr auto t3 = type_of(^^later);
// expected-error@-1 {{constexpr variable 't3' must be initialized by a constant expression}}
// expected-note@-2 {{cannot yet query the type of 'later': its return type has not been deduced}}
}  // namespace ns
