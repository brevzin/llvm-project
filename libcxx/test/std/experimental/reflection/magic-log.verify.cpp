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

// Where MAGIC_LOG and magic_log! (see magic-log.h) stop working.

#include "magic-log.h"

#include <string>

struct Widget {
  std::string get_name() const;

  // (a) is rejected in the explicit object and static member functions of a
  // class with get_name(): its (discarded) get_name(none...) finds only
  // non-static members, and there is no 'this'.
  void explicit_object(this Widget const& self) {
    MAGIC_LOG("explicit");
    // expected-error@-1 {{call to non-static member function without an object argument}}
  }
  static void static_member() {
    MAGIC_LOG("static");
    // expected-error@-1 {{call to non-static member function without an object argument}}
  }

  // (a) can't splice an explicit object parameter from inside a lambda.
  void explicit_from_lambda([[maybe_unused]] this Widget const& self) {
    [&] { MAGIC_LOG("in a lambda"); }();
    // expected-error@-1 {{call to non-static member function without an object argument}}
    // expected-error@-2 {{cannot splice local entity 'self' for which there is an intervening lambda expression}}
  }

  // For either, a lambda in between must capture the object.
  void implicit_without_capture() {
    [] { MAGIC_LOG("a"); }();
    // (Once for this lambda, and once for the one inside MAGIC_LOG.)
    // expected-error@-2 2 {{'this' cannot be implicitly captured in this context}}
    [] { magic_log!("b"); }();
    // expected-error@-1 {{'this' cannot be implicitly captured in this context}}
  }
  void explicit_without_capture([[maybe_unused]] this Widget const& self) {
    [] { magic_log!("c"); }();
    // expected-error@-1 {{variable 'self' cannot be implicitly captured in a lambda with no capture-default specified}}
  }

  // (b) names the explicit object parameter, so it must have a name.
  void unnamed(this Widget const&) {
    magic_log!("d");
    // expected-error@-1 {{expression macro 'magic_log<>' reported an error}}
    // expected-note@*:* {{the explicit object parameter must be named}}
  }

  // (b) takes a constant string, and nothing else, as the format string.
  void not_constant(char const* fmt) const {
    magic_log!(fmt);
    // expected-error@-1 {{expression macro 'magic_log<>' reported an error}}
    // expected-note@*:* {{the format string must be a constant string}}
  }
};
