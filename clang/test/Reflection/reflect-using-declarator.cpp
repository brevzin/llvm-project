//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -verify

// The reflection of a name found through a using-declarator is ill-formed;
// the error points at the name, qualified or not.

namespace ns {
enum class E { a };
struct S {};
} // namespace ns

using ns::E;
constexpr auto r1 = ^^E; // expected-error {{cannot take the reflection of a using-declarator}}

namespace other {
using ns::S;
}
constexpr auto r2 = ^^other::S; // expected-error {{cannot take the reflection of a using-declarator}}
