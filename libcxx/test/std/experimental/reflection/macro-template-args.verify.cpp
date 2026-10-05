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

// Explicit template arguments at a macro invocation: errors.

#include <meta>
#include <type_traits>

template <class E> requires std::is_enum_v<E>
__macro bitmask_type() { return ^^{}; } // #bitmask

__macro plain() { return ^^{}; }

enum class Permission : int { Read = 1 };

bitmask_type!<int>();
// expected-error@-1 {{no matching function for call to 'bitmask_type'}}
// expected-note@#bitmask {{candidate template ignored: constraints not satisfied [with E = int]}}

plain!<int>(); // expected-error {{'plain' does not name a template but is followed by template arguments}}

bitmask_type!<Permission>; // expected-error {{expected '('}}

template <class T> __macro size_of() { return ^^{ sizeof(\(^^T)) }; }
static_assert(size_of!<int, long>() == 4);
// expected-error@-1 {{no matching function for call to 'size_of'}}
// expected-note@-3 {{candidate template ignored: substitution failure: too many template arguments for function template 'size_of'}}
