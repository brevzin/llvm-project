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

// std::meta::id produces a single identifier token, so what its arguments
// spell must be an identifier: not 'operator|=' (which would otherwise
// declare an ordinary function of that odd name rather than an operator
// function), not a keyword, not empty.

#include <meta>

constexpr auto ok1 = std::meta::id("x", 1, "_y");
constexpr auto ok2 = std::meta::id("caf\u00e9");

constexpr auto bad1 = std::meta::id("operator", "|", "=");
// expected-error@-1 {{constexpr variable 'bad1' must be initialized by a constant expression}}
// expected-note@-2 {{std::meta::id produces 'operator|=', which is not an identifier}}

constexpr auto bad2 = std::meta::id("int");
// expected-error@-1 {{constexpr variable 'bad2' must be initialized by a constant expression}}
// expected-note@-2 {{std::meta::id produces 'int', which is a keyword}}

constexpr auto bad3 = std::meta::id("");
// expected-error@-1 {{constexpr variable 'bad3' must be initialized by a constant expression}}
// expected-note@-2 {{std::meta::id produces '', which is not an identifier}}

constexpr auto bad4 = std::meta::id("a b");
// expected-error@-1 {{constexpr variable 'bad4' must be initialized by a constant expression}}
// expected-note@-2 {{std::meta::id produces 'a b', which is not an identifier}}
