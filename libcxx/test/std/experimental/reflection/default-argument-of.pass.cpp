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

// RUN: %{build}
// RUN: %{exec} %t.exe

// std::meta::default_argument_of(p): the default argument has_default_argument
// finds, as an expression -- what an omitted argument is. Interpolated, it is
// not its tokens pasted: its names are bound and access is checked where it
// was written, it is instantiated if need be, and it is evaluated where it is
// interpolated, each time.

#include <meta>
#include <cassert>
#include <debugging>
#include <source_location>
#include <string_view>
#include <vector>

using std::meta::info;
using std::meta::token_sequence;

// The default argument of parameter I of F, as an omitted argument would be.
template <info F, int I>
__macro default_of() {
  return ^^{ \(default_argument_of(parameters_of(F)[I])) };
}

// Names are bound where the default argument was written.
namespace lib {
constexpr int limit = 42;
constexpr int f(int x = limit) { return x; }
} // namespace lib

// Access is checked there too.
class W {
  static constexpr int secret = 7;

public:
  static constexpr int g(int x = secret) { return x; }
};

// A function template's default argument is instantiated.
template <class T>
constexpr T h(T t = T{} + T(5)) {
  return t;
}

// Where it is used: the invocation.
constexpr unsigned line_of(std::source_location loc = std::source_location::current()) {
  return loc.line();
}

// Evaluated at each use.
int counter = 0;
int next() { return ++counter; }
int k(int a = next()) { return a; }

// call!(f, name = value, ...): any parameter left out uses its default
// argument, wherever it is.
struct named_arg {
  std::string_view name;
  token_sequence value;
};
consteval auto parse_named(token_sequence arg) -> named_arg {
  if (arg.size() < 3 || arg[1] != ^^{ = })
    std::constexpr_error_str("call", "expected 'name = value'");
  return {identifier_of(arg[0]), arg.subseq(2)};
}
__macro call(token_sequence fn, token_sequence... args) {
  auto r = std::meta::test_expression(^^{ ^^\(fn) });
  if (!r)
    std::constexpr_error_str("call", "the callee must name a single function");
  info f = extract<info>(constant_of(*r));
  std::vector<named_arg> named{parse_named(args)...};
  token_sequence list;
  std::size_t used = 0;
  bool first = true;
  for (info p : parameters_of(f)) {
    token_sequence arg;
    bool found = false;
    for (auto const& a : named)
      if (a.name == identifier_of(p)) {
        arg = a.value;
        found = true;
        ++used;
      }
    if (!found) {
      if (!has_default_argument(p))
        std::constexpr_error_str("call", "missing argument for a parameter");
      arg = ^^{ \(default_argument_of(p)) };
    }
    if (!first)
      list += ^^{ , };
    list += arg;
    first = false;
  }
  if (used != named.size())
    std::constexpr_error_str("call", "unknown argument name");
  return ^^{ \(fn)(\(list)) };
}

constexpr int two(int x = 0, int y = 0) { return 10 * x + y; }
constexpr int three(int a = 1, int b = 2, int c = 3) { return 100 * a + 10 * b + c; }

int main(int, char**) {
  {
    [[maybe_unused]] constexpr int limit = 0; // not this one
    static_assert(default_of!<^^lib::f, 0>() == 42);
    static_assert(lib::f(default_of!<^^lib::f, 0>()) == 42);
  }
  static_assert(default_of!<^^W::g, 0>() == 7);
  static_assert(default_of!<^^h<int>, 0>() == 5);
  static_assert(std::is_same_v<decltype(default_of!<^^h<long>, 0>()), long>);

  constexpr unsigned here = __LINE__; constexpr unsigned used = line_of(default_of!<^^line_of, 0>());
  static_assert(used == here);

  assert((default_of!<^^k, 0>() == 1));
  assert((default_of!<^^k, 0>() == 2));

  static_assert(call!(two, y = 2) == 2);
  static_assert(call!(two, x = 1) == 10);
  static_assert(call!(two) == 0);
  static_assert(call!(three, b = 9) == 193);
  static_assert(call!(three, c = 0, a = 5) == 520);

  static_assert(type_of(default_argument_of(parameters_of(^^h<double>)[0])) ==
                ^^double);
  return 0;
}
