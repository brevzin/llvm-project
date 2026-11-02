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

// members_of(namespace) represents each member once, however many times, and
// wherever, it is declared: in one namespace block, in several blocks, out of
// line (as a qualified name), or first as a friend.

#include <meta>

using std::meta::access_context;
using std::meta::info;

consteval auto count(info ns) -> std::size_t {
  return members_of(ns, access_context::unchecked()).size();
}

consteval auto only(info ns) -> info {
  auto ms = members_of(ns, access_context::unchecked());
  return ms.size() == 1 ? ms[0] : info();
}

// Several declarations in one block.
namespace same_block {
int f();
int f();
int f() { return 1; }
}  // namespace same_block
static_assert(only(^^same_block) == ^^same_block::f);

// A definition out of line.
namespace out_of_line {
int f();
}  // namespace out_of_line
int out_of_line::f() { return 1; }
static_assert(only(^^out_of_line) == ^^out_of_line::f);

// Declarations in reopened blocks.
namespace reopened {
int f();
}  // namespace reopened
namespace reopened {
int f();
}  // namespace reopened
namespace reopened {
int f() { return 1; }
}  // namespace reopened
static_assert(only(^^reopened) == ^^reopened::f);

// All of the above.
namespace mixed {
int f();
}  // namespace mixed
int mixed::f();
namespace mixed {
int f();
}  // namespace mixed
int mixed::f() { return 1; }
static_assert(only(^^mixed) == ^^mixed::f);

// Variables, classes, and templates likewise.
namespace var {
extern int v;
}  // namespace var
int var::v = 0;
namespace var {
extern int v;
}  // namespace var
static_assert(only(^^var) == ^^var::v);

namespace cls {
struct A;
}  // namespace cls
struct cls::A {};
namespace cls {
struct A;
}  // namespace cls
static_assert(only(^^cls) == ^^cls::A);

namespace fn_tmpl {
template <class T> T f();
}  // namespace fn_tmpl
template <class T> T fn_tmpl::f() { return T(); }
static_assert(only(^^fn_tmpl) == ^^fn_tmpl::f);

namespace cls_tmpl {
template <class T> struct X;
}  // namespace cls_tmpl
template <class T> struct cls_tmpl::X {};
static_assert(only(^^cls_tmpl) == ^^cls_tmpl::X);

// First declared as a friend (not a member until declared in the namespace).
namespace friend_first {
namespace inner {
struct S {
  friend void f();
};
}  // namespace inner
void inner::f() {}
}  // namespace friend_first
static_assert(count(^^friend_first) == 1);
static_assert(count(^^friend_first::inner) == 2);

namespace friend_then_reopened {
struct S {
  friend void f();
  friend void g() {}
};
}  // namespace friend_then_reopened
static_assert(count(^^friend_then_reopened) == 1);
namespace friend_then_reopened {
void f();
}  // namespace friend_then_reopened
namespace friend_then_reopened {
void f();
}  // namespace friend_then_reopened
static_assert(count(^^friend_then_reopened) == 2);

// A block-scope declaration doesn't make a function a member, nor keep its
// namespace-scope declaration from being one.
namespace local_extern {
void use() {
  extern void f();
}
void f();
}  // namespace local_extern
static_assert(count(^^local_extern) == 2);

// In a linkage specification.
namespace linkage {
extern "C++" {
void f();
}
void f();
}  // namespace linkage
namespace linkage {
extern "C++" void f();
}  // namespace linkage
static_assert(only(^^linkage) == ^^linkage::f);

// Defined out of line through an enclosing namespace of an inline namespace.
namespace outer {
inline namespace v1 {
void f();
}  // namespace v1
}  // namespace outer
void outer::f() {}
static_assert(only(^^outer) == ^^outer::v1);
static_assert(only(^^outer::v1) == ^^outer::v1::f);

int main() {}
