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

// std::meta::is_closure_type, like std::meta::is_class_type, is not a constant
// expression for anything but a type.

#include <meta>

constexpr auto lam = [] {};
namespace ns {}
template <class T> struct Tmpl {};

constexpr bool a = is_closure_type(^^lam);
// expected-error@-1 {{constexpr variable 'a' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a type}}

constexpr bool b = is_closure_type(^^decltype(lam)::operator());
// expected-error@-1 {{constexpr variable 'b' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a type}}

constexpr bool c = is_closure_type(^^ns);
// expected-error@-1 {{constexpr variable 'c' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a type}}

constexpr bool d = is_closure_type(^^Tmpl);
// expected-error@-1 {{constexpr variable 'd' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a type}}

constexpr bool e = is_closure_type(std::meta::info{});
// expected-error@-1 {{constexpr variable 'e' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a type}}

constexpr bool f = is_closure_type(std::meta::reflect_constant(lam));
// expected-error@-1 {{constexpr variable 'f' must be initialized by a constant expression}}
// expected-note@*:* {{expected a reflection of a type}}
