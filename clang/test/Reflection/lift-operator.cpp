//===----------------------------------------------------------------------===//
//
// Copyright 2024 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -verify

// Reflecting Types
using info = decltype(^^void);

consteval info info_void = ^^void;
consteval info info_decltype = ^^decltype(42);

using alias = int;
consteval info info_alias = ^^alias;

consteval info info_infoint = ^^decltype(^^int);

consteval info abominable = ^^void() const & noexcept;

// Reflecting variables
constexpr int i = 42;
consteval info info_i = ^^i;

// Reflecting templates
template <typename T>
void TemplateFunc();
consteval info info_template_func = ^^TemplateFunc;

template <typename T>
int TemplateVar;
consteval info info_template_var = ^^TemplateVar;

template <typename T>
struct TemplateStruct {};
consteval info info_template_struct = ^^TemplateStruct;

// Reflecting members of a class
struct Members {
    struct Type {};
    int member_var;
    static int static_member_var;

    int member_func();
    static int static_member_func();
};
consteval info info_nested_type = ^^Members::Type;
consteval info info_mem_var = ^^Members::member_var;
consteval info info_static_mem_var = ^^Members::static_member_var;
consteval info info_mem_func = ^^Members::member_func;
consteval info info_static_mem_func = ^^Members::static_member_func;

// Reflecting member templates
struct MemberTemplates {
    template <typename T>
    struct NestedTemplateStruct {};

    template <typename T>
    void template_func();

    template <typename T>
    MemberTemplates &operator+(const T&);

    template <typename T>
    static void template_static_func();

    template <typename T>
    static int template_var;
};
consteval info info_nested_template_struct =
      ^^MemberTemplates::NestedTemplateStruct;
consteval info info_nested_template_struct2 =
      ^^MemberTemplates::template NestedTemplateStruct;
consteval info info_nested_template_func = ^^MemberTemplates::template_func;
consteval info info_nested_template_func2 =
      ^^MemberTemplates::template template_func;
consteval info info_nested_template_static_func =
      ^^MemberTemplates::template_static_func;
consteval info info_nested_template_static_func2 =
      ^^MemberTemplates::template template_static_func;
consteval info info_nested_template_var = ^^MemberTemplates::template_var;
consteval info info_nested_template_var2 =
      ^^MemberTemplates::template template_var;
consteval info info_nested_template_operator = ^^MemberTemplates::operator+;
consteval info info_nested_template_operator2 =
      ^^MemberTemplates::template operator+;

template <typename T>
void DepScope() {
  consteval info nested_struct = ^^T::template NestedTemplateStruct;
  consteval info nested_func = ^^T::template template_func;
  consteval info nested_static_func = ^^T::template template_static_func;
  consteval info nested_var = ^^T::template template_var;
  consteval info nested_operator = ^^T::template operator+;
}
void InstantiateDepScope() {
  DepScope<MemberTemplates>();
}

// Reflecting function scope variables
void reflect_func_scope(int param) {
    consteval info info_param = ^^param;

    int local_var;
    consteval info info_local = ^^local_var;

    static int static_var;
    consteval info info_static = ^^static_var;

    thread_local int thread_var;
    consteval info info_thread = ^^thread_var;

    struct local_type {};
    consteval info info_type = ^^local_type;
}

// Reflecting a template parameter
template <typename T>
consteval info foo() {
    return ^^T;
}
consteval info info_tmplparam = foo<int>();

namespace ns {}
consteval info info_ns = ^^ns;
namespace ns {}
static_assert(info_ns == ^^ns);

// Reflection as a default initializer for a class member
class WithDefaultInitializer {
    [[maybe_unused]] info k = ^^::;
};
consteval WithDefaultInitializer with_default_init;

// Type with a nested name specifier that can't be parsed as an identifier.
namespace A { namespace B { using C = int; } }
consteval auto complicated_type = ^^A::B::C &;

                                // ============
                                // east_west_cv
                                // ============

namespace east_west_cv {
struct S {};
static_assert(^^S const == ^^const S);
static_assert(^^S volatile == ^^volatile S);
}  // namespace east_west_cv

                               // ==============
                               // self_reference
                               // ==============

namespace self_reference {
struct S {
  consteval S() {}

  decltype(^^::) k = ^^S::k;
};

consteval int fn(decltype(^^::) x = ^^x) { return 0; }
constexpr int x = fn();
}  // namspace self_reference

                              // =================
                              // enclosing_lambdas
                              // =================

namespace enclosing_lambdas {
static int s1;
void fn() {
  int l1;
  static int s2;
  consteval auto rl1 = ^^l1;
  (void) [] -> decltype(^^s1, ^^l1, s2) {
    // expected-error@-1 {{intervening lambda expression}}
    int l2;

    consteval auto rl1_2 = ^^l1;
      // expected-error@-1 {{intervening lambda expression}}
    consteval auto rl2 = ^^l2;
  };
}
}  // namespace enclosing_lambdas

                            // ====================
                            // requires_expressions
                            // ====================

namespace requires_expressions {
void fn(int p) {
    (void) requires(int a) { ^^p; };
    (void) requires(int a) { ^^a; };
      // expected-error@-1 {{local parameter of a requires-expression}}
}
}  // namespace requires_expressions

                             // ===================
                             // overload_resolution
                             // ===================

namespace overload_resolution {
template <typename T> void fn() requires (^^T != ^^int);
template <typename T> void fn() requires (^^T == ^^int);
template <typename T> void fn() requires (sizeof(T) == sizeof(int));

[[maybe_unused]] consteval auto a = ^^fn<char>;  // OK
}  // namespace overload_resolution

                   // =======================================
                   // bb_clang_p2996_issue_35_regression_test
                   // =======================================

namespace bb_clang_p2996_issue_35_regression_test {
template <auto R = ^^::> class S {};
S s;
}  // namespace bb_clang_p2996_issue_35_regression_test

                   // =======================================
                   // bb_clang_p2996_issue_73_regression_test
                   // =======================================

namespace bb_clang_p2996_issue_73_regression_test {
namespace foo {
struct foo {
  static_assert(^^foo == ^^::bb_clang_p2996_issue_73_regression_test::foo::foo);
};
static_assert(^^foo == ^^::bb_clang_p2996_issue_73_regression_test::foo::foo);
}  // namespace foo

namespace tfoo {
template <typename T> struct tfoo {
  static_assert(^^tfoo == ^^tfoo<T>);
};
static_assert(^^tfoo ==
              ^^::bb_clang_p2996_issue_73_regression_test::tfoo::tfoo);

tfoo<int> instantiation;
}  // namespace tfoo

static_assert(^^foo == ^^::bb_clang_p2996_issue_73_regression_test::foo);
static_assert(^^tfoo == ^^::bb_clang_p2996_issue_73_regression_test::tfoo);
}  // namespace bb_clang_p2996_issue_73_regression_test

                   // =======================================
                   // bb_clang_p2996_issue_11_regression_test
                   // =======================================

namespace bb_clang_p2996_issue_11_regression_test {
constexpr auto a = ^^:: != ^^int &;
constexpr auto b = ^^:: != ^^int &&;
constexpr auto c = ^^:: != ^^int() &;
constexpr auto d = ^^:: != ^^int() &&;

constexpr auto e = ^^:: != ^^int & true;
  //expected-error@-1 {{after top level declarator}} \
  //expected-warning@-1 {{'&' binds to reflection operand}}
constexpr auto f = ^^:: != ^^int && true;
  //expected-error@-1 {{after top level declarator}} \
  //expected-warning@-1 {{'&&' binds to reflection operand}}
constexpr auto g = ^^:: != ^^int() & true;
  //expected-error@-1 {{after top level declarator}} \
  //expected-warning@-1 {{'&' binds to reflection operand}}
constexpr auto h = ^^:: != ^^int() && true;
  //expected-error@-1 {{after top level declarator}} \
  //expected-warning@-1 {{'&&' binds to reflection operand}}

}  // namespace bb_clang_p2996_issue_11_regression_test
