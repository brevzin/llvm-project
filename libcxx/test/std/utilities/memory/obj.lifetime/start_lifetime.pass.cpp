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

#include <cassert>
#include <memory>
#include <string>
#include <type_traits>

static_assert(noexcept(std::start_lifetime(std::declval<int (&)[2]>())));
static_assert(std::is_same_v<decltype(std::start_lifetime(std::declval<int (&)[2]>())), void>);

// A fixed-capacity vector of a type with non-trivial special members, with
// trivial union storage whose elements' lifetimes are managed one by one.
template <class T, std::size_t N>
struct FixedVector {
  union {
    T storage[N];
  };
  std::size_t size = 0;

  constexpr FixedVector() { std::start_lifetime(storage); }
  constexpr FixedVector(const FixedVector& other) : FixedVector() {
    for (std::size_t i = 0; i != other.size; ++i)
      push_back(other.storage[i]);
  }
  constexpr ~FixedVector() { std::destroy(storage, storage + size); }

  constexpr void push_back(const T& v) { std::construct_at(storage + size++, v); }
  constexpr void pop_back() { std::destroy_at(storage + --size); }
};

constexpr bool test() {
  FixedVector<std::string, 4> v;
  v.push_back("hello");
  v.push_back("world, this string is too long for the small buffer");
  v.push_back("!");
  v.pop_back();
  assert(v.size == 2 && v.storage[0] == "hello");

  FixedVector<std::string, 4> w = v;
  assert(w.size == 2 && w.storage[1].starts_with("world"));

  // Starting the lifetime again changes nothing.
  std::start_lifetime(v.storage);
  assert(v.storage[0] == "hello");
  return true;
}

int main(int, char**) {
  test();
  static_assert(test());

  // A runtime call is a no-op.
  FixedVector<std::string, 2> v;
  std::start_lifetime(v.storage);
  v.push_back("x");
  assert(v.storage[0] == "x");
  return 0;
}
