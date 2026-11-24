// RUN: %clang_cc1 -std=c++2d -E -dM %s | FileCheck --check-prefix=CXX29 %s
// RUN: %clang_cc1 -std=c++2c -E -dM %s | FileCheck --check-prefix=NO-DO-EXPR %s
// RUN: %clang_cc1 -std=c++23 -E -dM %s | FileCheck --check-prefix=NO-DO-EXPR %s

// CXX29: #define __cpp_do_expressions 202611L
// NO-DO-EXPR-NOT: __cpp_do_expressions
