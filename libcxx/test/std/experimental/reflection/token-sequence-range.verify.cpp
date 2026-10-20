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

// Indexing a token_sequence out of range is not a constant expression: it
// reads outside the array of its tokens.

#include <meta>

constexpr std::meta::token_sequence ts = ^^{ a + b };

constexpr std::meta::token past_end = ts[3];
// expected-error@-1 {{constexpr variable 'past_end' must be initialized by a constant expression}}

constexpr std::meta::token negative = ts[-1];
// expected-error@-1 {{constexpr variable 'negative' must be initialized by a constant expression}}

constexpr std::meta::token from_empty = (^^{ })[0];
// expected-error@-1 {{constexpr variable 'from_empty' must be initialized by a constant expression}}

// A token_sequence is a value: its elements are read-only. (Build a new
// sequence with +=, +, and subseq.)
consteval bool assign() {
  std::meta::token_sequence seq = ^^{ a b };
  seq[0] = seq[1]; // expected-error {{cannot assign to return value because function 'operator[]' returns a const value}}
  return true;
}
