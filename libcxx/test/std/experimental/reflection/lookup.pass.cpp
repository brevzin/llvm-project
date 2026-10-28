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

// std::meta::lookup(scope, name, ctx): qualified name lookup, as for
// 'scope::name', reflected as ^^scope::name would be -- an entity or an
// overload set. Nothing when the lookup finds nothing, is ambiguous, or finds
// nothing accessible in ctx.

#include <meta>
#include <optional>
#include <string>
#include <string_view>

using std::meta::info;
using std::meta::operators;
using std::meta::lookup;
constexpr auto unchecked = std::meta::access_context::unchecked();
constexpr auto unprivileged = std::meta::access_context::unprivileged();

namespace N {
int var;
constexpr int one(int) { return 1; }
constexpr int two(int) { return 1; }
constexpr int two(double) { return 2; }
struct C {};
using Alias = C;
template <class T> struct Tmpl {};
namespace Inner {}
enum Unscoped { e1, e2 };
inline namespace In {
int in_inline;
}
} // namespace N

namespace M {
int via_directive;
}
namespace D {
using namespace M;
}

// Namespaces: each kind of result, as ^^ gives it.
static_assert(lookup(^^N, "var", unchecked) == ^^N::var);
static_assert(lookup(^^N, "one", unchecked) == ^^N::one);
static_assert(lookup(^^N, "two", unchecked) == ^^N::two);
static_assert(is_overload_set(*lookup(^^N, "two", unchecked)));
static_assert(lookup(^^N, "C", unchecked) == ^^N::C);
static_assert(lookup(^^N, "Alias", unchecked) == ^^N::Alias);
static_assert(lookup(^^N, "Tmpl", unchecked) == ^^N::Tmpl);
static_assert(lookup(^^N, "Inner", unchecked) == ^^N::Inner);
static_assert(lookup(^^N, "e2", unchecked) == ^^N::e2);
static_assert(lookup(^^N, "in_inline", unchecked) == ^^N::in_inline);
static_assert(lookup(^^D, "via_directive", unchecked) == ^^M::via_directive);
static_assert(!lookup(^^N, "nope", unchecked));
static_assert(lookup(^^::, "N", unchecked) == ^^N);

// The result splices like the name.
static_assert([: *lookup(^^N, "two", unchecked) :](1.0) == 2);
static_assert(sizeof(typename[: *lookup(^^N, "C", unchecked) :]) ==
              sizeof(N::C));

// A computed name, not only a literal: in a string's own buffer, on the heap,
// or in a local array.
consteval bool computed_names() {
  std::string small = "var";
  std::string large = "var";
  large.reserve(100); // now in a heap allocation
  const char buf[] = {'o', 'n', 'e'};
  // (Parenthesized: '^^N::var && ...' misparses; a separate parser issue.)
  return (lookup(^^N, small, unchecked) == ^^N::var) &&
         (lookup(^^N, large, unchecked) == ^^N::var) &&
         (lookup(^^N, std::string_view(buf, 3), unchecked) == ^^N::one) &&
         (lookup(^^N, u8"var", unchecked) == ^^N::var);
}
static_assert(computed_names());

// Classes.
struct Base {
  constexpr int f(int) const { return 1; }
  constexpr int g(int) const { return 1; }
  static constexpr int s = 3;
  using type = int;
};
struct Derived : Base {
  constexpr int f(double) const { return 2; } // hides Base::f
  using Base::g;
  constexpr int g(double) const { return 2; } // joins Base::g
};
static_assert(lookup(^^Derived, "s", unchecked) == ^^Base::s);
static_assert(lookup(^^Derived, "type", unchecked) == ^^Base::type);
static_assert(lookup(^^Derived, "f", unchecked) == ^^Derived::f);
static_assert(!is_overload_set(*lookup(^^Derived, "f", unchecked)));
static_assert(is_overload_set(*lookup(^^Derived, "g", unchecked)));
static_assert(overloads_of(*lookup(^^Derived, "g", unchecked)).size() == 2);
static_assert(Derived{}.[: *lookup(^^Derived, "g", unchecked) :](1) == 1);

// Ambiguous: nothing.
struct A1 { int m; };
struct A2 { int m; };
struct Both : A1, A2 {};
static_assert(!lookup(^^Both, "m", unchecked));

// Access: nothing inaccessible; of an overloaded name, the accessible ones.
class Priv {
  [[maybe_unused]] int hidden;
  constexpr int h(double) { return 2; }
public:
  constexpr int h(int) { return 1; }
};
static_assert(!lookup(^^Priv, "hidden", unprivileged));
static_assert(lookup(^^Priv, "hidden", unchecked));
static_assert(!is_overload_set(*lookup(^^Priv, "h", unprivileged)));
static_assert(is_overload_set(*lookup(^^Priv, "h", unchecked)));

// Enumerations.
enum class Scoped { a, b };
static_assert(lookup(^^Scoped, "b", unchecked) == ^^Scoped::b);

// Operators.
struct Op {
  constexpr bool operator==(const Op &) const { return true; }
  constexpr Op operator+(int) const { return {}; }
};
namespace OpNS {
struct X {};
constexpr bool operator<(X, X) { return true; }
constexpr bool operator<(X, int) { return false; }
} // namespace OpNS
static_assert(lookup(^^Op, operators::op_equals_equals, unchecked) ==
              ^^Op::operator==);
static_assert(lookup(^^Op, operators::op_plus, unchecked));
static_assert(!lookup(^^Op, operators::op_minus, unchecked));
static_assert(is_overload_set(*lookup(^^OpNS, operators::op_less, unchecked)));

// A class template specialization is instantiated first.
template <class T> struct Box { T value; };
static_assert(lookup(^^Box<int>, "value", unchecked) ==
              ^^Box<int>::value);

// In a class being defined: what's declared so far.
struct Partial {
  int first;
  consteval {
    static_assert(lookup(^^Partial, "first", unchecked));
    static_assert(!lookup(^^Partial, "second", unchecked));
  }
  int second;
};
static_assert(lookup(^^Partial, "second", unchecked));

int main(int, char **) { return 0; }
