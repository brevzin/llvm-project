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
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/TemplateStringAnnotation.h"

using namespace clang;

static CXXRecordDecl* CreateInterpolationsStruct(Sema &S,
                                                 CXXRecordDecl *StructDecl)
{
  ASTContext &Context = S.Context;
  SourceLocation Loc = StructDecl->getLocation();
  QualType CharConstPtrType = Context.getPointerType(Context.getConstType(Context.CharTy));

  CXXRecordDecl *InterpolationDecl = CXXRecordDecl::Create(
      Context, TagTypeKind::Struct, StructDecl, Loc, Loc,
      &Context.Idents.get("interpolation"));

  InterpolationDecl->startDefinition();

  // Add fields to interpolation struct
  QualType SizeTType = Context.getSizeType();

  // char const* expression;
  FieldDecl *ExpressionField = FieldDecl::Create(
      Context, InterpolationDecl, Loc, Loc,
      &Context.Idents.get("expression"),
      CharConstPtrType,
      Context.getTrivialTypeSourceInfo(CharConstPtrType, Loc),
      /*BitWidth=*/nullptr,
      /*Mutable=*/false,
      ICIS_NoInit);
  ExpressionField->setAccess(AS_public);
  InterpolationDecl->addDecl(ExpressionField);

  // char const* fmt;
  FieldDecl *FmtField = FieldDecl::Create(
      Context, InterpolationDecl, Loc, Loc,
      &Context.Idents.get("fmt"),
      CharConstPtrType,
      Context.getTrivialTypeSourceInfo(CharConstPtrType, Loc),
      /*BitWidth=*/nullptr,
      /*Mutable=*/false,
      ICIS_NoInit);
  FmtField->setAccess(AS_public);
  InterpolationDecl->addDecl(FmtField);

  // size_t index;
  // size_t count;
  for (char const* name : {"index", "count"}) {
    FieldDecl *IndexField = FieldDecl::Create(
        Context, InterpolationDecl, Loc, Loc,
        &Context.Idents.get(name),
        SizeTType,
        Context.getTrivialTypeSourceInfo(SizeTType, Loc),
        /*BitWidth=*/nullptr,
        /*Mutable=*/false,
        ICIS_NoInit);
    IndexField->setAccess(AS_public);
    InterpolationDecl->addDecl(IndexField);
  }

  InterpolationDecl->completeDefinition();

  return InterpolationDecl;
}

static VarDecl *CreateFmtVar(Sema &S,
                             const TemplateStringAnnotation& Annotation,
                             CXXRecordDecl* StructDecl)
{
  ASTContext &Context = S.Context;
  SourceLocation Loc = StructDecl->getLocation();

  QualType CharConstPtrType = Context.getPointerType(Context.getConstType(Context.CharTy));

  // Get the format string from the annotation
  StringLiteralParser Literal(Annotation.FormatString, S.getPreprocessor());
  StringRef FormatStr = Literal.GetString();

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
  return FmtVar;
}

static VarDecl *CreateStringsVar(Sema &S,
                                 const TemplateStringAnnotation& Annotation,
                                 CXXRecordDecl* StructDecl)
{
  ASTContext &Context = S.Context;
  SourceLocation Loc = StructDecl->getLocation();
  QualType CharConstPtrType = Context.getPointerType(Context.getConstType(Context.CharTy));

  size_t NumStrings = (Annotation.FormatString.size() + 1) / 2;

  // Create string literals for each string piece
  SmallVector<Expr*, 8> StringLiterals;
  for (size_t I = 0; I < Annotation.FormatString.size(); I += 2) {
    // Get the string piece from FormatString
    StringLiteralParser StrLiteral(Annotation.FormatString[I], S.getPreprocessor());
    StringRef StrPiece = StrLiteral.GetString();

    // Create the string literal type
    QualType StrTy = Context.getConstantArrayType(
        Context.CharTy.withConst(),
        llvm::APInt(32, StrPiece.size() + 1),
        nullptr, ArraySizeModifier::Normal, 0);

    // Create the string literal
    StringLiteral *StrLit = StringLiteral::Create(
        Context, StrPiece, StringLiteralKind::Ordinary, false, StrTy, {Loc});

    // Convert array to pointer
    ImplicitCastExpr *ArrayToPtr = ImplicitCastExpr::Create(
        Context, CharConstPtrType, CK_ArrayToPointerDecay, StrLit,
        nullptr, VK_PRValue, FPOptionsOverride());

    StringLiterals.push_back(ArrayToPtr);
  }

  // Create the array type: char const* [N]
  QualType ArrayType = Context.getConstantArrayType(
      CharConstPtrType,
      llvm::APInt(32, NumStrings),
      nullptr, ArraySizeModifier::Normal, 0);

  // Create the initializer list for the array
  InitListExpr *ArrayInit = new (Context) InitListExpr(
      Context, Loc, StringLiterals, Loc);
  ArrayInit->setType(ArrayType);

  // Create the static data member: static inline constexpr char const* strings[N] = {...}
  VarDecl *StringsVar = VarDecl::Create(
      Context, StructDecl, Loc, Loc,
      &Context.Idents.get("strings"),
      ArrayType,
      Context.getTrivialTypeSourceInfo(ArrayType, Loc),
      SC_Static);

  // Set as constexpr and inline
  StringsVar->setConstexpr(true);
  StringsVar->setInlineSpecified();
  StringsVar->setImplicit(true);
  StringsVar->setAccess(AS_public);
  StringsVar->setInit(ArrayInit);
  StringsVar->setInitStyle(VarDecl::CInit);
  StringsVar->markUsed(Context);

  return StringsVar;
}

static VarDecl *CreateInterpolationsVar(Sema &S,
                                        const TemplateStringAnnotation &Annotation,
                                        CXXRecordDecl* StructDecl,
                                        CXXRecordDecl* InterpolationDecl)
{
  ASTContext &Context = S.Context;
  SourceLocation Loc = StructDecl->getLocation();
  QualType CharConstPtrType = Context.getPointerType(Context.getConstType(Context.CharTy));

  size_t NumInterpolations = Annotation.Interpolations.size();

  // Get the type for the interpolation struct
  QualType InterpolationType = Context.getRecordType(InterpolationDecl);

  // Create array of interpolation initializers
  SmallVector<Expr*, 8> InterpolationInits;

  auto CreateIntegerLit = [&, SizeTType = Context.getSizeType()](size_t I){
    return IntegerLiteral::Create(
        Context, llvm::APInt(Context.getTypeSize(SizeTType), I),
        SizeTType, Loc);
  };

  for (size_t I = 0; I < NumInterpolations; ++I) {
    // Create string literal for the expression text
    auto tokens_to_string = [&](ArrayRef<Token> toks) -> std::string {
      auto const& SM = S.getSourceManager();

      auto const B = SM.getSpellingLoc(toks.front().getLocation());
      auto const EndTok = toks.back().getLocation();
      auto const End = Lexer::getLocForEndOfToken(EndTok, 0, SM, S.getLangOpts());

      auto const text = Lexer::getSourceText(CharSourceRange::getCharRange(B, End), SM, S.getLangOpts());

      std::string out;
      llvm::raw_string_ostream os(out);
      os.write_escaped(text, /*UseHexEscapes=*/true);
      os.flush();
      return out;
    };

    size_t CurIndex = Annotation.Interpolations[I];
    size_t NextIndex = (I + 1 < Annotation.Interpolations.size())
        ? Annotation.Interpolations[I + 1]
        : Annotation.ExpressionTokens.size();

    std::string ExprText = tokens_to_string(Annotation.ExpressionTokens[CurIndex]);

    QualType ExprStrTy = Context.getConstantArrayType(
        Context.CharTy.withConst(),
        llvm::APInt(32, ExprText.size() + 1),
        nullptr, ArraySizeModifier::Normal, 0);

    StringLiteral *ExprStrLit = StringLiteral::Create(
        Context, ExprText, StringLiteralKind::Ordinary, false, ExprStrTy, {Loc});

    ImplicitCastExpr *ExprToPtr = ImplicitCastExpr::Create(
        Context, CharConstPtrType, CK_ArrayToPointerDecay, ExprStrLit,
        nullptr, VK_PRValue, FPOptionsOverride());

    // Create string literal for the format specifier
    StringLiteralParser FmtLiteral(Annotation.FormatString[I * 2 + 1], S.getPreprocessor());
    StringRef FmtText = FmtLiteral.GetString();

    QualType FmtStrTy = Context.getConstantArrayType(
        Context.CharTy.withConst(),
        llvm::APInt(32, FmtText.size() + 1),
        nullptr, ArraySizeModifier::Normal, 0);

    StringLiteral *FmtStrLit = StringLiteral::Create(
        Context, FmtText, StringLiteralKind::Ordinary, false, FmtStrTy, {Loc});

    ImplicitCastExpr *FmtToPtr = ImplicitCastExpr::Create(
        Context, CharConstPtrType, CK_ArrayToPointerDecay, FmtStrLit,
        nullptr, VK_PRValue, FPOptionsOverride());

    // Create the index and count values
    IntegerLiteral *IndexLit = CreateIntegerLit(CurIndex);
    IntegerLiteral *CountLit = CreateIntegerLit(NextIndex - CurIndex);

    // Create initializer list for this interpolation struct
    Expr* FieldInits[] = {ExprToPtr, FmtToPtr, IndexLit, CountLit};

    InitListExpr *InterpolationInit = new (Context) InitListExpr(
        Context, Loc, FieldInits, Loc);
    InterpolationInit->setType(InterpolationType);

    InterpolationInits.push_back(InterpolationInit);
  }

  // Create the array type: interpolation[N]
  QualType InterpolationArrayType = Context.getConstantArrayType(
      InterpolationType,
      llvm::APInt(32, NumInterpolations),
      nullptr, ArraySizeModifier::Normal, 0);

  // Create the initializer list for the array
  InitListExpr *InterpolationsArrayInit = new (Context) InitListExpr(
      Context, Loc, InterpolationInits, Loc);
  InterpolationsArrayInit->setType(InterpolationArrayType);

  // Create the static data member: static inline constexpr interpolation interpolations[N] = {...}
  VarDecl *InterpolationsVar = VarDecl::Create(
      Context, StructDecl, Loc, Loc,
      &Context.Idents.get("interpolations"),
      InterpolationArrayType,
      Context.getTrivialTypeSourceInfo(InterpolationArrayType, Loc),
      SC_Static);

  // Set as constexpr and inline
  InterpolationsVar->setConstexpr(true);
  InterpolationsVar->setInlineSpecified();
  InterpolationsVar->setImplicit(true);
  InterpolationsVar->setAccess(AS_public);
  InterpolationsVar->setInit(InterpolationsArrayInit);
  InterpolationsVar->setInitStyle(VarDecl::CInit);
  InterpolationsVar->markUsed(Context);
  return InterpolationsVar;
}

ExprResult Sema::ActOnTemplateStringLiteral(SourceLocation Loc,
                                            const TemplateStringAnnotation& Annotation,
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

  // Create nested interpolation struct
  CXXRecordDecl* InterpolationDecl = CreateInterpolationsStruct(*this, StructDecl);
  StructDecl->addDecl(InterpolationDecl);

  // Create static constexpr inline char const* fmt
  // This is a string literal that is the full format specifier
  VarDecl *FmtVar = CreateFmtVar(*this, Annotation, StructDecl);
  StructDecl->addDecl(FmtVar);

  // Create static inline constexpr array of char const* named "strings"
  // The array contains string literals from every other element of FormatString
  VarDecl *StringsVar = CreateStringsVar(*this, Annotation, StructDecl);
  StructDecl->addDecl(StringsVar);

  // Create static inline constexpr array of interpolation structs
  // static inline constexpr interpolation interpolations[N] = {...}
  VarDecl *InterpolationsVar = CreateInterpolationsVar(*this, Annotation, StructDecl, InterpolationDecl);
  StructDecl->addDecl(InterpolationsVar);

  // Add fields for each expression
  SmallVector<Decl*, 4> FieldDecls;
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
    FieldDecls.push_back(Field);
  }

  // Properly process the fields and complete the class definition
  // This will determine special member functions based on field types
  ActOnFields(/*S=*/nullptr, Loc, StructDecl, FieldDecls, Loc, Loc,
              ParsedAttributesView{});
  CheckCompletedCXXClass(/*S=*/nullptr, StructDecl);

  // Add the struct to the DeclContext so it gets emitted
  DC->addDecl(StructDecl);

  // Push the static data members to ensure they get emitted
  Consumer.HandleTopLevelDecl(DeclGroupRef(FmtVar));
  Consumer.HandleTopLevelDecl(DeclGroupRef(StringsVar));
  Consumer.HandleTopLevelDecl(DeclGroupRef(InterpolationsVar));

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
  Expr *CompoundLit = new (Context) CompoundLiteralExpr(
      Loc, Context.getTrivialTypeSourceInfo(StructType, Loc),
      StructType, VK_PRValue, InitList, false);

  // Wrap in CXXBindTemporaryExpr if the type has a non-trivial destructor
  // This ensures proper cleanup and generation of the destructor
  return MaybeBindToTemporary(CompoundLit);
}
