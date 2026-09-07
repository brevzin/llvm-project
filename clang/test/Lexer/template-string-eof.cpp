// RUN: not %clang_cc1 -std=c++26 -freflection -fsyntax-only %s 2>&1 | FileCheck %s
// CHECK: error: missing terminating '"' character in template string literal
int x;
auto s = t"{x
