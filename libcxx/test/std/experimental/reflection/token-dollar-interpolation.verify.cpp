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

// '$' interpolation in a token sequence: errors.

#include <meta>

using namespace std::meta;

consteval token_sequence keyword() {
  return ^^{ $this }; // expected-error {{'$' interpolates a name, not the keyword 'this'; write '${this}'}}
}
consteval token_sequence digit() {
  return ^^{ $1 }; // expected-error {{'$' followed by a digit is reserved in a token sequence}}
}
consteval token_sequence lone() {
  return ^^{ $ + 1 }; // expected-error {{expected a name or '{' after '$' in a token sequence}}
}
consteval token_sequence too_many() {
  int x = 1;
  return ^^{ $$x }; // expected-error {{interpolation with 2 '$' escapes the enclosing token sequence (1 level of token sequence enclose it)}}
}
consteval token_sequence undeclared() {
  return ^^{ $nope }; // expected-error {{use of undeclared identifier 'nope'}}
}
