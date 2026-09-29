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

// A declaration macro invoked in a class template expands per
// specialization, as 'consteval { queue_injection(...); }' would: in place
// among the members, under the access in effect at the invocation.

#include <debugging>
#include <meta>
#include <string>

// Declare 'decl' only if 'c' holds.
__macro maybe(bool c, std::meta::token_sequence decl) {
  if (extract<bool>(constant_of(c)))
    return ^^{ \(decl); };
  return ^^{};
}

template <bool B>
struct C {
  int x;
  maybe!(B, int y);
};
static_assert(sizeof(C<true>) == 2 * sizeof(int));
static_assert(sizeof(C<false>) == sizeof(int));
static_assert(C<true>{1, 2}.y == 2);
static_assert(nonstatic_data_members_of(^^C<false>,
                                        std::meta::access_context::current())
                  .size() == 1);

// Paired with a mem-initializer macro: a member that is declared only for
// some specializations, and initialized when it is.
__macro init_if_member(std::meta::token_sequence init) {
  std::meta::info ctor = std::meta::macro_expansion_context();
  std::string_view name = identifier_of(init[0]);
  for (std::meta::info m : nonstatic_data_members_of(
           parent_of(ctor), std::meta::access_context::unchecked()))
    if (has_identifier(m) && identifier_of(m) == name)
      return init;
  return ^^{};
}

template <class T, class U>
class D {
  T always_;
  maybe!(sizeof(T) > 1, U sometimes_);

public:
  constexpr D(T t, [[maybe_unused]] U u)
      : always_(t), init_if_member!(sometimes_(u)) {}

  constexpr T always() const { return always_; }
  // The pattern cannot name the injected member; a dependent expression can.
  template <class Self>
  constexpr auto sometimes(this Self const& self) {
    return self.sometimes_;
  }
};
static_assert(D<int, std::string>(1, "two").always() == 1);
static_assert(D<int, std::string>(1, "two").sometimes() == "two");
static_assert(D<char, std::string>('a', "two").always() == 'a');
static_assert(sizeof(D<char, std::string>) == sizeof(char));
static_assert(is_private(nonstatic_data_members_of(
    ^^D<int, long>, std::meta::access_context::unchecked())[1]));

// The macro sees the specialization, including the members declared before
// the invocation.
__macro mirror() {
  std::meta::info cls = std::meta::macro_expansion_context();
  std::meta::token_sequence out = ^^{};
  for (std::meta::info m :
       nonstatic_data_members_of(cls, std::meta::access_context::unchecked()))
    out += ^^{ \(type_of(m)) \(std::meta::id(identifier_of(m), "_copy")); };
  return out;
}

template <class T>
struct E {
  T a;
  long b;
  mirror!();
};
static_assert(__is_same(decltype(E<char>::a_copy), char));
static_assert(__is_same(decltype(E<char>::b_copy), long));
static_assert(sizeof(E<long>) == 4 * sizeof(long));

int main(int, char**) {
  C<true> c{1, 2};
  D<int, std::string> d(1, "two");
  return (c.y == 2 && d.sometimes() == "two") ? 0 : 1;
}
