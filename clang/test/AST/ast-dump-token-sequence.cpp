// RUN: %clang_cc1 -std=c++26 -freflection -ast-dump -ast-dump-filter=tokens %s | FileCheck %s

#include "../Reflection/Inputs/token-sequence.h"

namespace std::meta {
}

constexpr int value = 42;

std::meta::token_sequence tokens() {
  return ^^{ int x = \(value); };
}

// CHECK: FunctionDecl {{.*}} tokens
// CHECK: CXXTokenSequenceExpr
// CHECK-NEXT: `-DeclRefExpr {{.*}} 'const int' lvalue Var {{.*}} 'value' 'const int'
