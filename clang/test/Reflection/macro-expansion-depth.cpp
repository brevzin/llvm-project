// Copyright 2026 Jump Trading, LLC
//
// RUN: %clang_cc1 %s -std=c++26 -freflection -fsyntax-only -verify=ok
// RUN: %clang_cc1 %s -std=c++26 -freflection -fsyntax-only -verify=self -DSELF
// RUN: %clang_cc1 %s -std=c++26 -freflection -fsyntax-only -verify=mutual -DMUTUAL
// RUN: %clang_cc1 %s -std=c++26 -freflection -fsyntax-only -verify=decl -DDECL
// RUN: %clang_cc1 %s -std=c++26 -freflection -fsyntax-only -verify=limit -DLIMIT -fmacro-expansion-depth=10
// RUN: %clang_cc1 %s -std=c++26 -freflection -fsyntax-only -verify=ok -DDEEP -fmacro-expansion-depth=1200 -Wno-stack-exhausted
// RUN: %clang -### -fmacro-expansion-depth=17 -c %s 2>&1 | FileCheck --check-prefix=DRIVER %s

// Macro expansion depth is bounded by -fmacro-expansion-depth (default 256).
// A macro that expands to itself used to recurse until the stack overflowed.

// ok-no-diagnostics
// DRIVER: "-fmacro-expansion-depth=17"

// Legitimate recursion: count!(C<N>{}) expands N levels deep.
template <int N> struct C { static constexpr int value = N; };
template <class T> __macro count(T&& n) {
  if constexpr (T::value == 0)
    return ^^{ 0 };
  else
    return ^^{ (1 + count!(C<\(T::value - 1)>{})) };
}
#ifndef LIMIT
static_assert(count!(C<200>{}) == 200);
#endif

#ifdef DEEP
// Well past the default, given a larger limit (and the stack for it).
static_assert(count!(C<1000>{}) == 1000);
#endif

#ifdef LIMIT
static_assert(count!(C<5>{}) == 5);
static_assert(count!(C<20>{}) == 20);
// limit-error@-1 {{recursive expansion of expression macro 'count<C<10>>' exceeded maximum depth of 10}}
// limit-note@*:* {{use -fmacro-expansion-depth=N to increase the maximum depth of nested expression macro expansions}}
#endif

#ifdef SELF
__macro loop(int x) { return ^^{ loop!(\(x)) }; }
int a = loop!(1);
// self-error@-1 {{recursive expansion of expression macro 'loop' exceeded maximum depth of 256}}
// self-note@*:* {{use -fmacro-expansion-depth=N to increase the maximum depth of nested expression macro expansions}}
#endif

#ifdef MUTUAL
__macro ping(int x);
__macro pong(int x) { return ^^{ ping!(\(x)) }; }
__macro ping(int x) { return ^^{ pong!(\(x)) }; }
int b = ping!(1);
// mutual-error@-1 {{recursive expansion of expression macro}}
// mutual-note@*:* {{use -fmacro-expansion-depth=N}}
#endif

#ifdef DECL
// Declaration-position expansions count too.
__macro decls(int x) { return ^^{ decls!(\(x)); }; }
decls!(1);
// decl-error@-1 {{recursive expansion of expression macro 'decls' exceeded maximum depth of 256}}
// decl-note@*:* {{use -fmacro-expansion-depth=N}}
#endif
