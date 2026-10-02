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

// Where a pack of raw macro parameters is (and is not) allowed.

#include <meta>

using std::meta::token_sequence;

// It takes the remaining arguments, so it comes last.
__macro not_last(token_sequence... args, token_sequence last); // expected-error {{a pack of raw macro parameters must be the last parameter}}

// Operator syntax supplies expression operands only.
struct S {};
__macro operator+(S const&, token_sequence... rhs); // expected-error {{an operator expression macro cannot have a token sequence parameter}}

// Only a pack of token sequences themselves, in a macro.
__macro by_reference(token_sequence&... args); // expected-error {{type 'token_sequence &' (aka 'meta::token_sequence &') of function parameter pack does not contain any unexpanded parameter packs}}
void not_a_macro(token_sequence... args); // expected-error {{type 'token_sequence' (aka 'meta::token_sequence') of function parameter pack does not contain any unexpanded parameter packs}}
