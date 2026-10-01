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
// ADDITIONAL_COMPILE_FLAGS: -std=c++2d

// P3549: noreturn_t arms are ignored, but the remaining arms of a std::visit
// visitor must still agree on a single return type.

#include <utility>
#include <variant>

void mismatch() {
  std::variant<int, long, char> v = 1;
  // expected-error-re@*:* {{static assertion failed{{.*}}`std::visit` requires the visitor to have a single return type, ignoring arms that return noreturn_t.}}
  (void)std::visit(
      [](auto x) {
        if constexpr (sizeof(x) == 1)
          std::unreachable();
        else
          return x; // int and long
      },
      v);
}
