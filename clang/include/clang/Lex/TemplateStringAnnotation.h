//===--- TemplateStringAnnotation.h - Template String Data -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TemplateStringAnnotation structure that holds data
// for template string literals.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_TEMPLATESTRINGANNOTATION_H
#define LLVM_CLANG_LEX_TEMPLATESTRINGANNOTATION_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

namespace clang {

/// Data stored in a template string annotation token.
/// Contains the format string and tokens for the embedded expressions.
struct TemplateStringAnnotation {
  /// Format String Pieces. For something like t"The value is {x}.", there will
  /// be two pieces: "The value is {" and "}.".
  llvm::SmallVector<std::vector<char>, 8> FormatStringData;
  std::vector<Token> FormatString;

  /// Tokens for each expression in the template string
  /// These are grouped by expression - each inner vector contains
  /// all tokens for one expression
  llvm::SmallVector<llvm::SmallVector<Token, 8>, 4> ExpressionTokens;

  /// Source location of the original template string literal
  SourceLocation Loc;

  TemplateStringAnnotation(SourceLocation L)
      : Loc(L) {}
};

} // end namespace clang

#endif // LLVM_CLANG_LEX_TEMPLATESTRINGANNOTATION_H
