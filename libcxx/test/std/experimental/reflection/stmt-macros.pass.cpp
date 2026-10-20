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
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d -Wempty-body -Wextra-semi-stmt

// RUN: %{build}
// RUN: %{exec} %t.exe

// Statement macros: 'name!(args);' as a whole statement at block scope. The
// expansion, followed by the invocation's ';', is parsed as statements in
// place. Directly in a block they join it -- declarations they introduce live
// to the end of the block -- and as a substatement they form one compound
// statement, as if braced.

#include <meta>
#include <cassert>
#include <string>

#include "test_macros.h"

std::string trace;

template <class F>
struct scope_guard {
  F f;
  ~scope_guard() { f(); }
};
template <class F>
scope_guard(F) -> scope_guard<F>;

// The guard lives to the end of the enclosing block. Its name is the
// placeholder '_', so any number of them can share a block.
__macro defer(std::meta::token_sequence body) {
  return ^^{ scope_guard _{[&] { \(body) }} };
}

// ------------------------------------------------------------- defer! -------

void basic() {
  trace += "a";
  defer!{ trace += "1"; };
  defer!{ trace += "2"; };
  trace += "b";
}

int early_return(bool early) {
  defer!{ trace += "x"; };
  if (early)
    return 1;
  trace += "y";
  return 2;
}

void in_loop() {
  for (int i = 0; i != 3; ++i) {
    defer!{ trace += char('0' + i); };
    trace += ".";
  }
}

#ifndef TEST_HAS_NO_EXCEPTIONS
void throws() {
  defer!{ trace += "unwound"; };
  throw 1;
}
#endif

// As a substatement, the expansion is its own scope: the guard runs at the
// end of the substatement, as 'if (c) scope_guard _{...};' would.
void substatement(bool c) {
  if (c)
    defer!{ trace += "s"; };
  trace += "t";
}

// -------------------------------------------------- statement sequences ----

// Several statements; the last one terminated by the invocation's ';'. (Each
// argument is interpolated once: an interpolation evaluates it.)
__macro bump_both(int& x, int& y) {
  return ^^{ ++\(x); ++\(y) };
}

// An 'if' without an 'else': the source's 'else' must not attach to it.
__macro when_positive(int x, std::meta::token_sequence then) {
  return ^^{ if (\(x) > 0) { \(then) } };
}

// Expands to nothing.
__macro nothing() { return ^^{}; }

void sequences() {
  int a = 0, a2 = 0;
  bump_both!(a, a2);
  assert(a == 1 && a2 == 1);

  int b = 0, b2 = 0;
  if (a == 1)
    bump_both!(b, b2);  // both statements are the substatement
  else
    b = -1;
  assert(b == 1 && b2 == 1);

  int c = 0, c2 = 0;
  if (a == 3)
    bump_both!(c, c2);
  assert(c == 0 && c2 == 0);  // neither statement ran

  int d = 0;
  if (a == 1)
    when_positive!(-1, d = 1;);  // the inner 'if' is false...
  else
    d = 2;                       // ...but this 'else' belongs to the outer 'if'
  assert(d == 0);

  if (a == 1)
    nothing!();  // no -Wempty-body
  nothing!();    // no -Wextra-semi-stmt
}

// ------------------------------------------- do-while vs do-expression ----

// A do-while statement stays one; a do-expression (what an expression macro
// typically expands to) is an expression statement, although at the start of
// a statement 'do' would begin a do-while.
__macro loop_until(int& x, int n) {
  return ^^{ do { } while (++\(x) < \(n)) };
}
__macro do_block(int& x) {
  return ^^{ do { ++\(x); } };
}

template <class T>
T do_forms(T x) {
  do_block!(x);
  return x;
}

void do_forms() {
  int a = 0;
  do_block!(a);
  assert(a == 1);
  if (a == 1)
    do_block!(a);
  assert(a == 2);
  assert(do_forms(10) == 11);

  int b = 0;
  loop_until!(b, 3);  // a do-while: the body runs until b reaches 3
  assert(b == 3);
}

// --------------------------------------------------- scope-transparent -----

// A declaration whose name the user wrote is visible after the invocation.
__macro let(std::meta::token_sequence name, int init) {
  return ^^{ auto \(name) = \(init) };
}

int transparent() {
  let!(x, 40);
  let!(y, 2);
  return x + y;
}

// ------------------------------------------------------------ templates ----

// Dependent arguments: expanded per instantiation, in place among the
// function's statements, with its locals visible.
template <class T>
std::string tmpl(T t) {
  std::string local = "L";
  defer!{ trace += local; };
  T u = t, w = t;
  bump_both!(u, w);
  trace += std::to_string(u + w);
  return local;
}

// Generic lambda: a templated context too.
void generic_lambda() {
  auto f = [](auto v) {
    defer!{ trace += "g"; };
    trace += std::to_string(v);
  };
  f(7);
}

// A statement macro that waits for instantiation although its arguments are
// not dependent.
__macro declare_late(std::meta::token_sequence name) {
  std::meta::expand_at_instantiation();
  return ^^{ int \(name) = 42 };
}

__macro id(std::meta::token_sequence name) {
  std::meta::expand_at_instantiation();
  return name;
}

template <class T>
int late() {
  declare_late!(n);
  return id!(n);
}

// ------------------------------------------------- more about templates ---

// Deferred (type-dependent arguments) as a substatement: the statements the
// instantiation injects belong to the 'if', not to the enclosing block.
template <class T>
T deferred_substatement(bool c, T x) {
  T y = x;
  if (c)
    bump_both!(x, y);
  else
    x = T();
  return x + y;
}

// Deferred inside a generic lambda.
int deferred_in_lambda() {
  auto f = [](auto v) {
    auto w = v;
    bump_both!(v, w);
    return v + w;
  };
  return f(10);
}

// ------------------------------------------------------- nested macros -----

// An expansion that itself invokes statement macros: they join the same
// block, so the inner guard lives to the end of the invoking block.
__macro traced_scope(std::meta::token_sequence name) {
  return ^^{
    trace += "<" \(name);
    defer!{ trace += ">"; };
  };
}

void nested() {
  {
    traced_scope!("a");
    trace += ".";
  }
  trace += "|";
}

// ---------------------------------------------------------- control flow ---

// 'return', 'continue' and 'break' in an expansion refer to the enclosing
// function and loop.
__macro return_if(bool c, int v) {
  return ^^{ if (\(c)) return \(v) };
}
__macro skip_if(bool c) {
  return ^^{ if (\(c)) continue };
}
__macro stop_if(bool c) {
  return ^^{ if (\(c)) break };
}

int control_flow(int n) {
  return_if!(n < 0, -1);
  int sum = 0;
  for (int i = 0; i != 10; ++i) {
    skip_if!(i % 2 == 1);
    stop_if!(i > n);
    sum += i;
  }
  return sum;
}

// ------------------------------------------- consteval block in expansion --

// Statements injected by a consteval block inside the expansion land in
// place among the expansion's own.
__macro with_injection() {
  return ^^{
    trace += "a";
    consteval { queue_injection(^^{ trace += "c"; }); }
    trace += "b"
  };
}

void injection() {
  with_injection!();
}

// ---------------------------------------- expression macros as statements --

int calls = 0;
__macro count_call(int& n) {
  return ^^{ ++\(n) };
}

void expression_macro_statement() {
  count_call!(calls);  // still just an expression statement
  if (calls == 1)
    count_call!(calls);
  assert(calls == 2);
}

int main(int, char**) {
  basic();
  assert(trace == "ab21");

  trace.clear();
  assert(early_return(true) == 1);
  assert(early_return(false) == 2);
  assert(trace == "xyx");

  trace.clear();
  in_loop();
  assert(trace == ".0.1.2");

#ifndef TEST_HAS_NO_EXCEPTIONS
  trace.clear();
  try {
    throws();
  } catch (int) {
  }
  assert(trace == "unwound");
#endif

  trace.clear();
  substatement(true);
  substatement(false);
  assert(trace == "stt");

  sequences();
  do_forms();
  assert(transparent() == 42);

  trace.clear();
  assert(tmpl(5) == "L");
  assert(trace == "12L");

  trace.clear();
  generic_lambda();
  assert(trace == "7g");

  assert(late<int>() == 42);

  expression_macro_statement();

  assert(deferred_substatement(true, 1) == 4);
  assert(deferred_substatement(false, 1) == 1);
  assert(deferred_in_lambda() == 22);

  trace.clear();
  nested();
  assert(trace == "<a.>|");

  assert(control_flow(-5) == -1);
  assert(control_flow(4) == 0 + 2 + 4);
  assert(control_flow(100) == 0 + 2 + 4 + 6 + 8);

  trace.clear();
  injection();
  assert(trace == "acb");
  return 0;
}
