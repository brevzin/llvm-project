//===--- SemaTemplateString.cpp - Template String Literal Handling -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements semantic analysis for template string literals.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/Sema.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"

using namespace clang;


ExprResult Sema::ActOnTemplateStringLiteral(SourceLocation Loc, StringRef FormatStr,
                                           ArrayRef<ExprResult> Exprs) {
  // Create an anonymous struct type with:
  // - static constexpr char const* fmt() = "...";
  // - decltype((expr)) _0, _1, ...

  // Create the anonymous struct at namespace scope to allow static members
  // Find the nearest namespace or translation unit context
  DeclContext *DC = CurContext;
  while (DC && !DC->isFileContext() && !DC->isNamespace())
    DC = DC->getParent();
  if (!DC)
    DC = Context.getTranslationUnitDecl();

  // Create a new record (struct) declaration
  CXXRecordDecl *StructDecl = CXXRecordDecl::Create(
      Context, TagTypeKind::Struct, DC, Loc, Loc,
      /*Id=*/nullptr);

  StructDecl->startDefinition();

  QualType CharConstPtrType = Context.getPointerType(
    Context.getConstType(Context.CharTy));

  // Add the fmt static member function
  // Create the string literal for the format string
  QualType FmtStrTy = Context.getConstantArrayType(
      Context.CharTy.withConst(),
      llvm::APInt(32, FormatStr.size() + 1),
      nullptr, ArraySizeModifier::Normal, 0);

  StringLiteral *FmtLit = StringLiteral::Create(
      Context, FormatStr, StringLiteralKind::Ordinary, false, FmtStrTy, {Loc});

  // Create static constexpr inline data member fmt
  // static inline constexpr char const* fmt = FmtLit;
  VarDecl *FmtVar = VarDecl::Create(
      Context, StructDecl, Loc, Loc,
      &Context.Idents.get("fmt"),
      CharConstPtrType,
      Context.getTrivialTypeSourceInfo(CharConstPtrType, Loc),
      SC_Static);

  // Set as constexpr and inline
  FmtVar->setConstexpr(true);
  FmtVar->setInlineSpecified();
  FmtVar->setImplicit(true);
  FmtVar->setAccess(AS_public);

  // Initialize with the same array-to-pointer conversion as the function
  ImplicitCastExpr *SFmtInit = ImplicitCastExpr::Create(
      Context, CharConstPtrType, CK_ArrayToPointerDecay, FmtLit,
      nullptr, VK_PRValue, FPOptionsOverride());

  FmtVar->setInit(SFmtInit);
  FmtVar->setInitStyle(VarDecl::CInit);
  FmtVar->markUsed(Context);

  // Add the static data member to the struct
  StructDecl->addDecl(FmtVar);

  // Add fields for each expression
  SmallVector<FieldDecl*, 4> Fields;
  for (size_t I = 0; I < Exprs.size(); ++I) {
    if (Exprs[I].isInvalid())
      return ExprError();

    Expr *E = Exprs[I].get();

    // Get the type using decltype((E))
    QualType FieldType = Context.getReferenceQualifiedType(E);

    // Create field name like _0, _1, etc.
    SmallString<16> FieldName;
    FieldName = "_";
    FieldName += std::to_string(I);

    FieldDecl *Field = FieldDecl::Create(
        Context, StructDecl, Loc, Loc,
        &Context.Idents.get(FieldName),
        FieldType,
        Context.getTrivialTypeSourceInfo(FieldType, Loc),
        /*BitWidth=*/nullptr,
        /*Mutable=*/false,
        ICIS_NoInit);

    Field->setAccess(AS_public);
    StructDecl->addDecl(Field);
    Fields.push_back(Field);
  }

  StructDecl->completeDefinition();

  // Add the struct to the DeclContext so it gets emitted
  DC->addDecl(StructDecl);

  // Push the static data member to ensure it gets emitted
  Consumer.HandleTopLevelDecl(DeclGroupRef(FmtVar));

  // Create the type for the struct
  QualType StructType = Context.getRecordType(StructDecl);

  // Create an initializer list expression with the expressions
  SmallVector<Expr*, 4> InitExprs;
  for (auto E : Exprs) {
    if (E.isInvalid())
      return ExprError();
    InitExprs.push_back(E.get());
  }

  // Create the compound literal expression
  InitListExpr *InitList = new (Context) InitListExpr(
      Context, Loc, InitExprs, Loc);
  InitList->setType(StructType);

  // Create a compound literal (C99) or CXXTemporaryObjectExpr (C++)
  // For now, we'll use a compound literal
  return new (Context) CompoundLiteralExpr(
      Loc, Context.getTrivialTypeSourceInfo(StructType, Loc),
      StructType, VK_PRValue, InitList, false);
}
