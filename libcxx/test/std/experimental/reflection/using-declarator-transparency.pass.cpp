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

// Namespace-scope using-declarators are transparent to the reflection operator
// (P4466, generalized to any namespace): '^^N::x' reflects the declaration
// that the using-declarator names, so everything about the reflection is that
// declaration's, not the using-declarator's.

#include <cstdint>
#include <meta>

namespace ns {
struct S {};
template <class T> struct Tmpl {};
void f();
using Alias = int;
} // namespace ns

namespace other {
using ns::S;
using ns::Tmpl;
using ns::f;
using ns::Alias;
} // namespace other

// The motivating example: libc++ declares 'std::int8_t' with
// 'namespace std { using ::int8_t; }'.
static_assert(^^std::int8_t == ^^::int8_t);
static_assert(std::meta::parent_of(^^std::int8_t) == ^^::);
static_assert(std::meta::is_type_alias(^^std::int8_t));

static_assert(std::meta::parent_of(^^other::S) == ^^ns);
static_assert(std::meta::parent_of(^^other::f) == ^^ns);
static_assert(std::meta::parent_of(^^other::Alias) == ^^ns);
static_assert(std::meta::parent_of(^^other::Tmpl) == ^^ns);

static_assert(std::meta::identifier_of(^^other::S) == "S");
static_assert(std::meta::identifier_of(^^other::Alias) == "Alias");
static_assert(std::meta::is_type_alias(^^other::Alias));
static_assert(std::meta::dealias(^^other::Alias) == ^^int);
static_assert(!std::meta::is_type_alias(^^other::S));
static_assert(std::meta::is_class_type(^^other::S));
static_assert(std::meta::is_function(^^other::f));
static_assert(std::meta::is_class_template(^^other::Tmpl));

static_assert(std::meta::template_of(^^other::Tmpl<int>) == ^^ns::Tmpl);
static_assert(std::meta::template_of(^^ns::Tmpl<int>) == ^^other::Tmpl);
static_assert(std::meta::substitute(^^other::Tmpl, {^^int}) == ^^ns::Tmpl<int>);

// Splicing the reflection names the declaration itself.
static_assert(std::is_same_v<typename [:^^other::S:], ns::S>);
static_assert(&[:^^other::f:] == &ns::f);

void ns::f() {}

int main() {}
