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
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d -Wextra-semi -Wempty-body

// RUN: %{build}
// RUN: %{exec} %t.exe

// The ';' of a declaration macro invocation, 'name!(args);', belongs to the
// invocation: the expansion is parsed followed by it. A macro returns a
// fragment with no terminator -- the same fragment an expression macro
// would -- and an empty expansion leaves an empty declaration. A macro that
// does end its expansion in ';' (or expands to nothing) leaves a ';' that is
// not diagnosed as extra, even with -Wextra-semi.

#include <meta>

// Declare 'decl' only if 'c' holds; no ';' of its own.
__macro maybe(bool c, std::meta::token_sequence decl) {
  if (extract<bool>(constant_of(c)))
    return decl;
  return ^^{};
}

// The older style, which supplies the ';' itself.
__macro maybe_terminated(bool c, std::meta::token_sequence decl) {
  if (extract<bool>(constant_of(c)))
    return ^^{ \(decl); };
  return ^^{};
}

// A class definition: the invocation's ';' completes it.
__macro make_struct(std::meta::token_sequence name, int v) {
  return ^^{ struct \(name) { static constexpr int value = \(v); } };
}

// Several declarations, the last one terminated by the invocation.
__macro two_ints(std::meta::token_sequence a, std::meta::token_sequence b) {
  return ^^{ int \(a) = 1; int \(b) = 2 };
}

// ------------------------------------------------------ namespace scope ----

maybe!(true, int ns_yes = 1);
maybe!(false, int ns_no = 2);
maybe_terminated!(true, int ns_yes_old = 3);
maybe_terminated!(false, int ns_no_old = 4);
make_struct!(Made, 7);
two_ints!(first, second);

// -------------------------------------------------- non-template class ----

struct Plain {
  int x;
  maybe!(true, int y);
  maybe!(false, int z);
  maybe_terminated!(true, int y_old);
  maybe_terminated!(false, int z_old);
  make_struct!(Nested, 9);
};
static_assert(sizeof(Plain) == 3 * sizeof(int));

// ------------------------------------------------------- class template ----

template <bool B>
struct Tmpl {
  int x;
  maybe!(B, int y);
  maybe_terminated!(B, int y_old);
  make_struct!(Nested, 11);
};
static_assert(sizeof(Tmpl<true>) == 3 * sizeof(int));
static_assert(sizeof(Tmpl<false>) == sizeof(int));

int main(int, char**) {
  if (ns_yes != 1 || ns_yes_old != 3 || first != 1 || second != 2)
    return 1;
  if (Made::value != 7 || Plain::Nested::value != 9 ||
      Tmpl<true>::Nested::value != 11 || Tmpl<false>::Nested::value != 11)
    return 1;
  Plain p{1, 2, 3};
  if (p.y != 2 || p.y_old != 3)
    return 1;
  Tmpl<true> t{1, 2, 3};
  if (t.y != 2 || t.y_old != 3)
    return 1;
  return 0;
}
