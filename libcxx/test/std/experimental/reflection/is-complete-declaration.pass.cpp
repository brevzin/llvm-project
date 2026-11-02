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
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d -Wno-defaulted-function-deleted

// Some parts of a function's declaration aren't known until later:
// - the return type deduced from the body of a member function defined in its
//   class,
// - the noexcept-specifier and the default arguments of such a function,
// - whether a function defaulted in its class is deleted, and its implicit
//   exception specification.
// The body, noexcept-specifier, and default arguments are parsed once the
// outermost enclosing class is complete. Whether a defaulted function is
// deleted is decided once its own class is complete.
//
// Querying one of these parts before then is not a constant expression (see
// not-yet-known.verify.cpp). It doesn't give an answer that changes later.
// std::meta::is_complete_declaration says whether any part is still unknown.
//
// A member function whose definition is written but not yet processed is
// members-of-representable. Only what depends on its return type is unknown.

#include <meta>
#include <string_view>

using std::meta::access_context;
using std::meta::info;

consteval auto has_minus_via_members_of(info ty) -> bool {
  for (info f : members_of(ty, access_context::unchecked()))
    if (is_operator_function(f) and
        operator_of(f) == std::meta::operators::op_minus)
      return true;
  return false;
}

consteval auto member_named(info ty, std::string_view name) -> info {
  for (info m : members_of(ty, access_context::unchecked()))
    if (has_identifier(m) && identifier_of(m) == name)
      return m;
  return {};
}

consteval auto nth_member(info ty, std::size_t i) -> info {
  return members_of(ty, access_context::unchecked())[i];
}

// The motivating example: an 'auto' member, asked about in its own class.
struct AutoMinus {
  auto operator-(AutoMinus) { return 42; }

  static_assert(has_minus_via_members_of(^^AutoMinus));
  static_assert(!is_complete_declaration(^^AutoMinus::operator-));
  // What doesn't depend on the return type is known.
  static_assert(is_public(^^AutoMinus::operator-));
  static_assert(parameters_of(^^AutoMinus::operator-).size() == 1);
};
static_assert(has_minus_via_members_of(^^AutoMinus));
static_assert(is_complete_declaration(^^AutoMinus::operator-));
static_assert(return_type_of(^^AutoMinus::operator-) == ^^int);
static_assert(type_of(^^AutoMinus::operator-) == ^^int(AutoMinus));

// A written return type is known.
struct IntMinus {
  auto operator-(IntMinus) -> int { return 42; }
  static_assert(is_complete_declaration(^^IntMinus::operator-));
  static_assert(return_type_of(^^IntMinus::operator-) == ^^int);
};

// Hidden friends, likewise.
struct Friendly {
  friend auto operator-(Friendly, Friendly) { return 0; }
  static_assert(hidden_friends_of(^^Friendly).size() == 1);
  static_assert(!is_complete_declaration(hidden_friends_of(^^Friendly)[0]));
};
static_assert(is_complete_declaration(hidden_friends_of(^^Friendly)[0]));

// noexcept-specifiers and default arguments that are parsed late.
struct Late {
  void g(int a = sizeof(Late)) noexcept(sizeof(Late) > 0);
  void h(int a = 1);
  void k() noexcept;

  static_assert(!is_complete_declaration(^^Late::g));
  static_assert(!is_complete_declaration(parameters_of(^^Late::g)[0]));
  static_assert(has_default_argument(parameters_of(^^Late::g)[0]));

  // A default argument with no parentheses or commas is parsed late too.
  static_assert(!is_complete_declaration(^^Late::h));

  // Plain 'noexcept' is parsed immediately.
  static_assert(is_complete_declaration(^^Late::k));
  static_assert(is_noexcept(^^Late::k));
  static_assert(type_of(^^Late::k) == ^^void() noexcept);
};
static_assert(is_complete_declaration(^^Late::g));
static_assert(is_complete_declaration(parameters_of(^^Late::g)[0]));
static_assert(is_noexcept(^^Late::g));
static_assert(is_complete_declaration(^^Late::h));

// A defaulted function: whether it's deleted, and its exception
// specification, are decided once its class is complete.
struct Throws { Throws() noexcept(false); };
struct NoDefault { NoDefault(int); };
struct Defaulted {
  Defaulted() = default;
  Defaulted(Defaulted const&) noexcept = default;
  Throws t;

  static_assert(!is_complete_declaration(nth_member(^^Defaulted, 0)));
  // Its exception specification is written, but whether it's deleted isn't
  // known yet.
  static_assert(!is_complete_declaration(nth_member(^^Defaulted, 1)));
};
static_assert(is_complete_declaration(nth_member(^^Defaulted, 0)));
static_assert(!is_noexcept(nth_member(^^Defaulted, 0)));
static_assert(!is_deleted(nth_member(^^Defaulted, 0)));

struct DeletedDefault {
  DeletedDefault() = default;  // deleted: NoDefault has no default ctor
  NoDefault n;
};
static_assert(is_deleted(nth_member(^^DeletedDefault, 0)));

// A complete nested class whose member bodies are still waiting for the
// enclosing class to complete.
struct Outer {
  struct Inner {
    auto f() { return 1; }
  };
  static_assert(member_named(^^Inner, "f") != info());
  static_assert(!is_complete_declaration(^^Inner::f));
};
static_assert(is_complete_declaration(^^Outer::Inner::f));

// Inside a member function's body, functions defined before it have been
// parsed; it itself, and those defined after it, have not.
struct Order {
  auto before() { return 1; }
  auto self() {
    static_assert(is_complete_declaration(^^Order::before));
    static_assert(!is_complete_declaration(^^Order::self));
    static_assert(!is_complete_declaration(^^Order::after));
    static_assert(member_named(^^Order, "after") != info());
    return 2;
  }
  auto after() { return 3; }
};

// A member of a class template specialization.
template <class T>
struct Tmpl {
  auto f() { return T(); }
  static_assert(!is_complete_type(^^Tmpl));
  static_assert(!is_complete_declaration(^^Tmpl));
  static_assert(member_named(^^Tmpl, "f") != info());
  static_assert(!is_complete_declaration(member_named(^^Tmpl, "f")));
  // Naming it instantiates its definition, as decltype(self().f()) would.
  static_assert(is_complete_declaration(^^Tmpl::f));
};
template struct Tmpl<int>;
static_assert(is_complete_declaration(^^Tmpl<int>));
static_assert(is_complete_declaration(^^Tmpl<long>));
static_assert(is_complete_declaration(member_named(^^Tmpl<char>, "f")));
static_assert(return_type_of(^^Tmpl<char>::f) == ^^char);

// A function declared with a placeholder return type and defined later is
// unknown until then. It isn't members-of-representable, as in standard C++:
// its definition isn't written yet.
namespace ns {
auto later();
static_assert(!is_complete_declaration(^^later));
static_assert(members_of(^^ns, access_context::unchecked()).size() == 0);
auto later() { return 1; }
static_assert(is_complete_declaration(^^later));
}  // namespace ns

struct OutOfLine {
  auto f();
};
static_assert(!is_complete_declaration(^^OutOfLine::f));
static_assert(member_named(^^OutOfLine, "f") == info());
auto OutOfLine::f() { return 1; }
static_assert(is_complete_declaration(^^OutOfLine::f));
static_assert(member_named(^^OutOfLine, "f") != info());

// Types: a class or enumeration type is complete once defined. Every other
// type is as complete as it will ever be.
struct Fwd;
enum class Opaque : int;
static_assert(!is_complete_declaration(^^Fwd));
static_assert(is_complete_declaration(^^Opaque));
static_assert(is_complete_declaration(^^int));
static_assert(is_complete_declaration(^^void));
static_assert(is_complete_declaration(^^int[]));
struct Fwd {
  static_assert(!is_complete_declaration(^^Fwd));
};
static_assert(is_complete_declaration(^^Fwd));

// Everything else has no parts left to come.
int var;
namespace other {}
static_assert(is_complete_declaration(^^var));
static_assert(is_complete_declaration(^^other));
static_assert(is_complete_declaration(^^Tmpl));
static_assert(is_complete_declaration(info()));

int main() {}
