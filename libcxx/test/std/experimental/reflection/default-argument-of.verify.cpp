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

// std::meta::default_argument_of: only for a parameter that has a default
// argument (has_default_argument).

#include <meta>

void f(int x, int y = 1);

constexpr auto ok = std::meta::default_argument_of(parameters_of(^^f)[1]);

constexpr auto none = std::meta::default_argument_of(parameters_of(^^f)[0]);
// expected-error@-1 {{constexpr variable 'none' must be initialized by a constant expression}}

constexpr auto not_param = std::meta::default_argument_of(^^f);
// expected-error@-1 {{constexpr variable 'not_param' must be initialized by a constant expression}}
