// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// -ftime-trace shows where a macro's cost goes: evaluating its body, parsing
// its expansion, and the metafunctions the evaluation calls.
//
// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang_cc1 %s -std=c++26 -freflection -fsyntax-only \
// RUN:   -ftime-trace=%t/trace.json -ftime-trace-granularity=0
// RUN: %python -c 'import json, sys; [print(e["name"], e.get("args", {}).get("detail", "")) for e in json.load(open(sys.argv[1]))["traceEvents"] if e.get("ph") == "X"]' %t/trace.json \
// RUN:   | FileCheck %s

#include "Inputs/token-sequence.h"
using std::meta::token_sequence;
using info = decltype(^^int);

// CHECK-DAG: {{^}}MacroEvaluate N::make_struct{{$}}
// CHECK-DAG: {{^}}MacroParseExpansion{{ ?$}}
namespace N {
__macro make_struct(token_sequence name) {
  return ^^{ struct \(name) { int x; }; };
}
} // namespace N
N::make_struct!(S);
S s{1};

// CHECK-DAG: {{^}}Metafunction first_enumerator{{$}}
enum E { A, B };
consteval auto first_enumerator(info r) -> info {
  class Sentinel;
  // (Metafunction #0 is get_begin_enumerator_decl_of.)
  return __metafunction(0, r, ^^Sentinel);
}
constexpr info first = first_enumerator(^^E);
