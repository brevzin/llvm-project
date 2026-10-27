//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03, c++11, c++14, c++17, c++20, c++23

// <memory>

// template<class T>
//   constexpr void start_lifetime(T& r) noexcept;
//
// Mandates: T is a complete type and an implicit-lifetime aggregate type.
// (And, as libc++'s choice, not const or volatile.)

#include <memory>
#include <string>

struct NotAggregate {
  NotAggregate();
};
struct UserDestructor {
  int i;
  ~UserDestructor();
};

void f(NotAggregate& a, UserDestructor& b, std::string& c, const int (&d)[2]) {
  std::start_lifetime(a); // expected-error-re@*:* {{static assertion failed{{.*}}implicit-lifetime aggregate type}}
  std::start_lifetime(b); // expected-error-re@*:* {{static assertion failed{{.*}}implicit-lifetime aggregate type}}
  std::start_lifetime(c); // expected-error-re@*:* {{static assertion failed{{.*}}implicit-lifetime aggregate type}}
  std::start_lifetime(d); // expected-error-re@*:* {{static assertion failed{{.*}}const or volatile object}}
  // The builtin diagnoses each too.
  // expected-error@*:* 4 {{argument to '__builtin_start_lifetime' must be a pointer to}}
}
