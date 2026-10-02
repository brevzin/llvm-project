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

// A pack of raw macro parameters, 'std::meta::token_sequence... args' (P1219's
// homogeneous variadic function parameters, only for macros and only for raw
// parameters): each remaining argument, split at its top-level commas as any
// macro argument list is, as its own token sequence. The macro is a template,
// as with 'auto... args'; in its body the parameter is an ordinary pack.

#include <meta>
#include <algorithm>
#include <cassert>
#include <debugging>
#include <string_view>
#include <vector>

using std::meta::punctuator;
using std::meta::token_sequence;

// ---------------------------------------------------------------- basics ---

__macro count(token_sequence... args) {
  return ^^{ \(std::meta::reflect_constant(sizeof...(args))) };
}
static_assert(count!() == 0);
static_assert(count!(x) == 1);
static_assert(count!(a, b c, d) == 3);
// Commas inside brackets of every kind belong to their argument.
static_assert(count!(f(a, b), S{c, d}, [e, f] {}, g[h, i]) == 4);
// The braced form permits a trailing comma; it adds no argument.
static_assert(count!{a, b,} == 2);

// (A pack index must be a constant expression; a value computed in the body is
// not one, but a vector can be indexed with it.)
__macro nth(token_sequence n, token_sequence... args) {
  int i = extract<int>(constant_of(*std::meta::test_expression(n)));
  return std::vector<token_sequence>{args...}[i];
}

__macro last(token_sequence first, token_sequence... rest) {
  if constexpr (sizeof...(rest) == 0)
    return first;
  else
    return rest...[sizeof...(rest) - 1];
}
static_assert(last!(1) == 1);
static_assert(last!(1, 2, 3 + 4) == 7);
static_assert(nth!(1, 10, 20 + 2, 30) == 22);

// Expanding the pack: into a vector, and in a fold.
__macro sum_vector(token_sequence... args) {
  token_sequence out = ^^{ 0 };
  for (token_sequence a : std::vector<token_sequence>{args...})
    out += ^^{ + (\(a)) };
  return out;
}
static_assert(sum_vector!(1, 2, 3) == 6);
static_assert(sum_vector!() == 0);

__macro sum_fold(token_sequence... args) {
  return (^^{ 0 } + ... + (^^{ + } + args));
}
static_assert(sum_fold!(1, 2, 3, 4) == 10);

// After an expression parameter and a raw one. (The expression argument is
// interpolated once: each interpolation would evaluate it.)
__macro scale_sum(int factor, token_sequence op, token_sequence... args) {
  token_sequence out = ^^{ 0 };
  ((out += op + ^^{ (\(args)) }), ...);
  return ^^{ \(factor) * (\(out)) };
}
static_assert(scale_sum!(10, +, 1, 2, 3) == 60);

// ---------------------------------------------------- member and deferred --

struct Builder {
  int base;
  __macro add(this Builder const& self, token_sequence... args) {
    return (^^{ \(self).base } + ... + (^^{ + } + args));
  }
};

template <class T>
struct Holder {
  static __macro count(token_sequence... args) {
    return ^^{ \(std::meta::reflect_constant(sizeof...(args))) };
  }
};

template <class T>
constexpr int deferred() {
  // The class is dependent: the argument list waits for instantiation, then is
  // split according to the macro found.
  return T::count!(a, f(b, c), d);
}

// ------------------------------------------------------- statement macro ---

// Each argument is a statement.
__macro all(token_sequence... stmts) {
  return (^^{} + ... + (stmts + ^^{ ; }));
}

// --------------------------------------------- named arguments: call! ------

struct named_arg {
  std::string_view name;
  token_sequence value;
};

// One argument, 'name = value'.
consteval auto parse_named(token_sequence arg) -> named_arg {
  auto toks = tokens_of(arg);
  if (toks.size() < 3 ||
      token_kind_of(toks[0]) != std::meta::token_kind::identifier ||
      token_kind_of(toks[1]) != std::meta::token_kind::punctuator ||
      punctuator_of(toks[1]) != punctuator::equals)
    std::constexpr_error_str("call", "expected 'name = value'");
  token_sequence value = ^^{};
  for (std::size_t i = 2; i != toks.size(); ++i)
    value += toks[i];
  return {identifier_of(toks[0]), value};
}

// Call a function with its arguments named, in any order; trailing
// parameters with default arguments may be left out.
__macro call(token_sequence fn, token_sequence... args) {
  auto r = std::meta::test_expression(^^{ ^^\(fn) });
  if (!r)
    std::constexpr_error_str("call", "the callee must name a single function");
  std::meta::info f = extract<std::meta::info>(constant_of(*r));

  std::vector<named_arg> named{parse_named(args)...};
  token_sequence list = ^^{};
  std::size_t used = 0;
  for (std::meta::info p : parameters_of(f)) {
    bool found = false;
    for (auto const& a : named)
      if (a.name == identifier_of(p)) {
        list += (used ? ^^{ , } : ^^{}) + a.value;
        found = true;
        ++used;
      }
    if (!found) {
      if (!has_default_argument(p))
        std::constexpr_error_str("call", "missing argument for a parameter");
      break;
    }
  }
  if (used != named.size())
    std::constexpr_error_str("call", "unknown or unusable argument name");
  return ^^{ \(fn)(\(list)) };
}

int f(int x, int y) { return 10 * x + y; }
int g(int a, int b = 5, int c = 7) { return 100 * a + 10 * b + c; }
struct P {
  int a, b;
};
int sum(P p) { return p.a + p.b; }

int main(int, char**) {
  Builder b{100};
  assert(b.add!(1, 2) == 103);
  assert(b.add!() == 100);

  static_assert(deferred<Holder<int>>() == 3);

  int x = 0, y = 0;
  all!(x += 1, y = x * 10, x += y);
  assert(x == 11 && y == 10);

  assert(call!(f, y = 1, x = 2) == 21);
  assert(call!(g, a = 1) == 157);
  assert(call!(g, b = 2, a = 1) == 127);
  assert(call!(f, y = sum(P{1, 2}),
               x = [i = 2, j = 3] { return std::max(i, j); }()) == 33);
  return 0;
}
