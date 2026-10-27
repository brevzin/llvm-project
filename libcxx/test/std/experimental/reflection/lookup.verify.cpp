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

// std::meta::lookup: misuse is an error, not an empty result.

#include <meta>

constexpr auto ctx = std::meta::access_context::unchecked();
namespace N { int x; }
struct Incomplete;

constexpr auto bad_name = std::meta::lookup(^^N, "not an identifier", ctx);
// expected-error@-1 {{constexpr variable 'bad_name' must be initialized by a constant expression}}
// expected-note@-2 {{in call to}}
// expected-note@-3 {{'not an identifier' is not a valid identifier}}

constexpr auto bad_scope = std::meta::lookup(^^int, "x", ctx);
// expected-error@-1 {{constexpr variable 'bad_scope' must be initialized by a constant expression}}
// expected-note@-2 {{in call to}}
// expected-note@-3 {{expected a reflection of a namespace, complete class type, or enumeration type, but got a type}}

constexpr auto incomplete = std::meta::lookup(^^Incomplete, "x", ctx);
// expected-error@-1 {{constexpr variable 'incomplete' must be initialized by a constant expression}}
// expected-note@-2 {{in call to}}
// expected-note@-3 {{expected a reflection of a namespace, complete class type, or enumeration type, but got a type}}

// (Each failure is also noted at the metafunction call in <meta>.)
// expected-note@*:* 3 {{subexpression not valid in a constant expression}}
