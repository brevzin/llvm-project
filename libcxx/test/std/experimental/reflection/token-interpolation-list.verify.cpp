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

// A list interpolated into a token sequence: errors.

#include <meta>

using namespace std::meta;

// '...' needs a pack to expand.
__macro not_pack(token_sequence x) {
  return ^^{ (\(x...; ^^{ + })) }; // expected-error {{pack expansion does not contain any unexpanded parameter packs}}
}

// A pack must be expanded, wherever it appears in the list.
__macro unexpanded(token_sequence... xs) {
  return ^^{ (\(xs; ^^{ + })) }; // expected-error {{expression contains unexpanded parameter pack 'xs'}}
}
__macro unexpanded_second(token_sequence y, token_sequence... xs) {
  return ^^{ f(\(y, xs)) }; // expected-error {{expression contains unexpanded parameter pack 'xs'}}
}

// The separator is one value, not a pack.
__macro pack_separator(token_sequence... xs) {
  return ^^{ (\(xs...; xs)) }; // expected-error {{expression contains unexpanded parameter pack 'xs'}}
}

// A ';' introduces a separator, which must be written.
__macro missing_separator(token_sequence... xs) {
  return ^^{ (\(xs...; )) }; // expected-error {{expected expression}}
}
