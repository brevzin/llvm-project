//===--- Reflection.cpp - Classes for representing reflection ---*- C++ -*-===//
//
// Copyright 2024 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the ReflectionValue class.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Reflection.h"
#include "clang/AST/ASTContext.h"
#include "clang/Lex/Token.h"

namespace clang {

namespace {
struct MaterializedTokens {
  llvm::DenseMap<std::pair<const Token *, size_t>, UnnamedGlobalConstantDecl *>
      StorageFor;
  llvm::DenseMap<const Decl *, TokenSequenceData> TokensOf;
};
MaterializedTokens &materializedTokens() {
  static MaterializedTokens M;
  return M;
}
} // namespace

APValue MaterializeTokens(ASTContext &C, TokenSequenceData TSD,
                          QualType PtrTy) {
  size_t N = TSD.size();
  if (N == 0)
    return APValue(static_cast<const Expr *>(nullptr),
                   CharUnits::fromQuantity(C.getTargetNullPointerValue(PtrTy)),
                   APValue::NoLValuePath(), /*IsNullPtr=*/true);
  MaterializedTokens &M = materializedTokens();
  UnnamedGlobalConstantDecl *&Storage = M.StorageFor[{TSD.data(), N}];
  if (!Storage) {
    // Its own storage: tokens that compare equal may still differ (e.g.
    // interpolated constants of different types), so equal arrays are not
    // shared -- but the same array (the same storage) always is.
    // Each token refers to its slot in the token array (immutable, and
    // living as long as the compilation): no storage of its own.
    APValue Elems(APValue::UninitArray(), N, N);
    for (size_t I = 0; I != N; ++I)
      Elems.getArrayInitializedElt(I) =
          APValue(TokenSequenceData(TSD.data() + I, 1));
    QualType ArrTy = C.getConstantArrayType(
        C.MetaTokenTy.withConst(), llvm::APInt(64, N), nullptr,
        ArraySizeModifier::Normal, /*IndexTypeQuals=*/0);
    Storage = C.createUniqueUnnamedGlobalConstantDecl(ArrTy, Elems);
    M.TokensOf[Storage] = TSD;
  }
  APValue::LValuePathEntry Path[] = {APValue::LValuePathEntry::ArrayIndex(0)};
  return APValue(APValue::LValueBase(Storage), CharUnits::Zero(), Path,
                 /*OnePastTheEnd=*/false);
}

namespace {
// The buffers made by AppendTokenSequenceData, by their first token: how
// many tokens are in use, and how many fit.
struct GrowableBuffer {
  size_t Used;
  size_t Capacity;
};
llvm::DenseMap<const Token *, GrowableBuffer> &growableBuffers() {
  static llvm::DenseMap<const Token *, GrowableBuffer> Buffers;
  return Buffers;
}
} // namespace

TokenSequenceData AppendTokenSequenceData(ASTContext &Ctx,
                                          TokenSequenceData LHS,
                                          ArrayRef<Token> RHS) {
  if (RHS.empty())
    return LHS;
  auto &Buffers = growableBuffers();
  size_t N = LHS.size() + RHS.size();
  if (!LHS.empty()) {
    auto It = Buffers.find(LHS.data());
    if (It != Buffers.end() && It->second.Used == LHS.size() &&
        N <= It->second.Capacity) {
      // (RHS may be a view of this same buffer, e.g. ts += ts; it lies
      // within the used prefix, so it does not overlap what is written.)
      std::copy(RHS.begin(), RHS.end(),
                const_cast<Token *>(LHS.data()) + LHS.size());
      It->second.Used = N;
      return TokenSequenceData(LHS.data(), N);
    }
  }
  size_t Capacity = std::max<size_t>(2 * N, 16);
  Token *Stored = new (Ctx) Token[Capacity];
  std::copy(RHS.begin(), RHS.end(), std::copy(LHS.begin(), LHS.end(), Stored));
  Buffers[Stored] = {N, Capacity};
  return TokenSequenceData(Stored, N);
}

const TokenSequenceData *getMaterializedTokens(const Decl *Storage) {
  MaterializedTokens &M = materializedTokens();
  auto It = M.TokensOf.find(Storage);
  return It == M.TokensOf.end() ? nullptr : &It->second;
}

TokenSequenceData CreateTokenSequenceData(ASTContext &Ctx,
                                          ArrayRef<Token> Tokens) {
  Token *StoredTokens = nullptr;
  if (!Tokens.empty()) {
    StoredTokens = new (Ctx) Token[Tokens.size()];
    std::copy(Tokens.begin(), Tokens.end(), StoredTokens);
  }

  return TokenSequenceData(StoredTokens, Tokens.size());
}

TokenSequenceData CreateTokenSequenceData(ASTContext &Ctx,
                                          ArrayRef<Token> Tokens1,
                                          ArrayRef<Token> Tokens2) {

  Token *StoredTokens = nullptr;
  size_t Size = Tokens1.size() + Tokens2.size();
  if (Size != 0) {
    StoredTokens = new (Ctx) Token[Size];
    Token *Next = std::copy(Tokens1.begin(), Tokens1.end(), StoredTokens);
    std::copy(Tokens2.begin(), Tokens2.end(), Next);
  }

  return TokenSequenceData(StoredTokens, Size);
}

bool TagDataMemberSpec::operator==(TagDataMemberSpec const &Rhs) const {
  return (Ty == Rhs.Ty &&
          Alignment == Rhs.Alignment &&
          BitWidth == Rhs.BitWidth &&
          Name == Rhs.Name);
}

bool TagDataMemberSpec::operator!=(TagDataMemberSpec const &Rhs) const {
  return !(*this == Rhs);
}

bool FunctionDeclSpec::operator==(FunctionDeclSpec const &Rhs) const {
  return (Source == Rhs.Source && Name == Rhs.Name &&
          TemplateParameterPrefix == Rhs.TemplateParameterPrefix &&
          ParameterPrefix == Rhs.ParameterPrefix &&
          NameOperator == Rhs.NameOperator &&
          MarkOverride == Rhs.MarkOverride &&
          MarkNoexcept == Rhs.MarkNoexcept && MarkConst == Rhs.MarkConst &&
          MarkMacro == Rhs.MarkMacro);
}

bool FunctionDeclSpec::operator!=(FunctionDeclSpec const &Rhs) const {
  return !(*this == Rhs);
}

}  // end namespace clang

namespace clang {
static constexpr OverloadedOperatorKind MetaOperatorOrder[] = {
    OO_None, OO_New, OO_Delete, OO_Array_New, OO_Array_Delete, OO_Coawait,
    OO_Call, OO_Subscript, OO_Arrow, OO_ArrowStar, OO_Tilde, OO_Exclaim,
    OO_Plus, OO_Minus, OO_Star, OO_Slash, OO_Percent, OO_Caret, OO_Amp,
    OO_Pipe, OO_Equal, OO_PlusEqual, OO_MinusEqual, OO_StarEqual,
    OO_SlashEqual, OO_PercentEqual, OO_CaretEqual, OO_AmpEqual, OO_PipeEqual,
    OO_EqualEqual, OO_ExclaimEqual, OO_Less, OO_Greater, OO_LessEqual,
    OO_GreaterEqual, OO_Spaceship, OO_AmpAmp, OO_PipePipe, OO_LessLess,
    OO_GreaterGreater, OO_LessLessEqual, OO_GreaterGreaterEqual, OO_PlusPlus,
    OO_MinusMinus, OO_Comma,
};

OverloadedOperatorKind getOverloadedOperatorForMetaIndex(unsigned Index) {
  return Index < std::size(MetaOperatorOrder) ? MetaOperatorOrder[Index]
                                              : OO_None;
}

QualType stripDeducedTypeSugar(const ASTContext &C, QualType T) {
  if (const auto *DT = dyn_cast<DeducedType>(T)) {
    if (!DT->isDeduced())
      return T;
    return C.getQualifiedType(DT->getDeducedType(), T.getLocalQualifiers());
  }
  // A placeholder buried under a reference or pointer ('auto&&' deduced as
  // 'int&'): only the canonical type is free of it.
  if (const DeducedType *DT = T->getContainedDeducedType();
      DT && DT->isDeduced())
    return C.getCanonicalType(T);
  return T;
}

OverloadedOperatorKind getOverloadedOperatorForTokenKind(tok::TokenKind Kind) {
  switch (Kind) {
#define OVERLOADED_OPERATOR(Name, Spelling, Token, Unary, Binary, MemberOnly)  \
  case tok::Token:                                                             \
    return OO_##Name;
#define OVERLOADED_OPERATOR_MULTI(Name, Spelling, Unary, Binary, MemberOnly)
#include "clang/Basic/OperatorKinds.def"
  default:
    return OO_None;
  }
}

static constexpr tok::TokenKind MetaPunctuators[] = {
#define META_PUNCTUATOR(Name, TokenKind, Spelling) tok::TokenKind,
#include "clang/AST/MetaPunctuators.def"
};

unsigned getMetaIndexForPunctuator(tok::TokenKind Kind) {
  for (unsigned I = 0; I != std::size(MetaPunctuators); ++I)
    if (MetaPunctuators[I] == Kind)
      return I;
  return ~0u;
}

tok::TokenKind getPunctuatorForMetaIndex(unsigned Index) {
  return Index < std::size(MetaPunctuators) ? MetaPunctuators[Index]
                                            : tok::unknown;
}

unsigned getMetaIndexForOverloadedOperator(OverloadedOperatorKind OO) {
  const auto *It = llvm::find(MetaOperatorOrder, OO);
  return It == std::end(MetaOperatorOrder) ? 0 : It - MetaOperatorOrder;
}

} // namespace clang
