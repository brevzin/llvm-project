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

static CXXRecordDecl* CreateInterpolationsStruct(Sema &S)
{
  static CXXRecordDecl* InterpolationDecl = nullptr;
  if (InterpolationDecl != nullptr) {
    return InterpolationDecl;
  }

  ASTContext &Context = S.Context;
  SourceLocation Loc = SourceLocation();
  QualType CharConstPtrType = Context.getPointerType(Context.getConstType(Context.CharTy));

  InterpolationDecl = CXXRecordDecl::Create(
      Context, TagTypeKind::Struct, Context.getTranslationUnitDecl(), Loc, Loc,
      &Context.Idents.get("_Interpolation"));

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

static CXXMethodDecl *CreateFmtFunction(Sema &S,
                                        const TemplateStringAnnotation& Annotation,
                                        CXXRecordDecl* StructDecl)
{
  ASTContext &Context = S.Context;
  SourceLocation Loc = StructDecl->getLocation();

  QualType CharConstPtrType = Context.getPointerType(Context.getConstType(Context.CharTy));

  StringLiteralParser Literal(Annotation.FormatString, S.getPreprocessor());
  StringRef FormatStr = Literal.GetString();

  QualType FmtStrTy = Context.getConstantArrayType(
      Context.CharTy.withConst(),
      llvm::APInt(32, FormatStr.size() + 1),
      nullptr, ArraySizeModifier::Normal, 0);

  StringLiteral *FmtLit = StringLiteral::Create(
      Context, FormatStr, StringLiteralKind::Ordinary, false, FmtStrTy, {Loc});

  ImplicitCastExpr *FmtPtr = ImplicitCastExpr::Create(
      Context, CharConstPtrType, CK_ArrayToPointerDecay, FmtLit,
      nullptr, VK_PRValue, FPOptionsOverride());

  QualType FuncType = Context.getFunctionType(
      CharConstPtrType, {}, FunctionProtoType::ExtProtoInfo());

  CXXMethodDecl *FmtFunc = CXXMethodDecl::Create(
      Context, StructDecl, Loc,
      DeclarationNameInfo(Context.DeclarationNames.getIdentifier(
          &Context.Idents.get("fmt")), Loc),
      FuncType,
      Context.getTrivialTypeSourceInfo(FuncType, Loc),
      SC_Static,
      /*UsesFPIntrin=*/false,
      /*isInline=*/true,
      ConstexprSpecKind::Consteval,
      Loc);

  FmtFunc->setImplicit(true);
  FmtFunc->setAccess(AS_public);

  ReturnStmt *Return = ReturnStmt::Create(Context, Loc, FmtPtr, nullptr);
  FmtFunc->setBody(CompoundStmt::Create(Context, {Return}, FPOptionsOverride(), Loc, Loc));

  return FmtFunc;
}

static CXXMethodDecl *CreateStringFunction(Sema &S,
                                           const TemplateStringAnnotation& Annotation,
                                           CXXRecordDecl* StructDecl)
{
  ASTContext &Context = S.Context;
  SourceLocation Loc = StructDecl->getLocation();
  QualType CharConstPtrType = Context.getPointerType(Context.getConstType(Context.CharTy));
  QualType SizeTType = Context.getSizeType();

  size_t NumStrings = (Annotation.FormatString.size() + 1) / 2;

  auto CreateIntegerLit = [&](size_t I){
    return IntegerLiteral::Create(
        Context, llvm::APInt(Context.getTypeSize(SizeTType), I),
        SizeTType, Loc);
  };

  QualType FuncType = Context.getFunctionType(
      CharConstPtrType, {SizeTType}, FunctionProtoType::ExtProtoInfo());

  CXXMethodDecl *StringFunc = CXXMethodDecl::Create(
      Context, StructDecl, Loc,
      DeclarationNameInfo(Context.DeclarationNames.getIdentifier(
          &Context.Idents.get("string")), Loc),
      FuncType,
      Context.getTrivialTypeSourceInfo(FuncType, Loc),
      SC_Static,
      /*UsesFPIntrin=*/false,
      /*isInline=*/true,
      ConstexprSpecKind::Consteval,
      Loc);

  StringFunc->setImplicit(true);
  StringFunc->setAccess(AS_public);

  ParmVarDecl *NParam = ParmVarDecl::Create(
      Context, StringFunc, Loc, Loc,
      &Context.Idents.get("n"),
      SizeTType,
      Context.getTrivialTypeSourceInfo(SizeTType, Loc),
      SC_None, nullptr);
  NParam->setScopeInfo(0, 0);
  StringFunc->setParams({NParam});

  DeclRefExpr *NRef = DeclRefExpr::Create(
      Context, NestedNameSpecifierLoc(), Loc, NParam, false,
      DeclarationNameInfo(NParam->getDeclName(), Loc),
      SizeTType, VK_LValue);

  ImplicitCastExpr *NValue = ImplicitCastExpr::Create(
      Context, SizeTType, CK_LValueToRValue, NRef,
      nullptr, VK_PRValue, FPOptionsOverride());

  SmallVector<Stmt*, 8> BodyStmts;
  for (size_t I = 0; I < NumStrings; ++I) {
    StringLiteralParser StrLiteral(Annotation.FormatString[I * 2], S.getPreprocessor());
    StringRef StrPiece = StrLiteral.GetString();

    QualType StrTy = Context.getConstantArrayType(
        Context.CharTy.withConst(),
        llvm::APInt(32, StrPiece.size() + 1),
        nullptr, ArraySizeModifier::Normal, 0);

    StringLiteral *StrLit = StringLiteral::Create(
        Context, StrPiece, StringLiteralKind::Ordinary, false, StrTy, {Loc});

    ImplicitCastExpr *StrToPtr = ImplicitCastExpr::Create(
        Context, CharConstPtrType, CK_ArrayToPointerDecay, StrLit,
        nullptr, VK_PRValue, FPOptionsOverride());

    ReturnStmt *Return = ReturnStmt::Create(Context, Loc, StrToPtr, nullptr);

    IntegerLiteral *ILit = CreateIntegerLit(I);
    BinaryOperator *Cond = BinaryOperator::Create(
        Context, NValue, ILit, BO_EQ, Context.BoolTy, VK_PRValue, OK_Ordinary, Loc, FPOptionsOverride());

    IfStmt *If = IfStmt::Create(Context, Loc, IfStatementKind::Ordinary,
                                nullptr, nullptr, Cond, Loc, Loc, Return, Loc, nullptr);
    BodyStmts.push_back(If);
  }

  CXXNullPtrLiteralExpr *NullExpr = new (Context) CXXNullPtrLiteralExpr(CharConstPtrType, Loc);
  ReturnStmt *DefaultReturn = ReturnStmt::Create(Context, Loc, NullExpr, nullptr);
  BodyStmts.push_back(DefaultReturn);

  StringFunc->setBody(CompoundStmt::Create(Context, BodyStmts, FPOptionsOverride(), Loc, Loc));

  return StringFunc;
}

static CXXMethodDecl *CreateInterpolationFunction(Sema &S,
                                                  const TemplateStringAnnotation &Annotation,
                                                  CXXRecordDecl* StructDecl)
{
  ASTContext &Context = S.Context;
  SourceLocation Loc = StructDecl->getLocation();
  QualType CharConstPtrType = Context.getPointerType(Context.getConstType(Context.CharTy));
  QualType SizeTType = Context.getSizeType();

  size_t NumInterpolations = Annotation.Interpolations.size();

  QualType InterpolationType = Context.getRecordType(CreateInterpolationsStruct(S));

  auto CreateIntegerLit = [&](size_t I){
    return IntegerLiteral::Create(
        Context, llvm::APInt(Context.getTypeSize(SizeTType), I),
        SizeTType, Loc);
  };

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

  QualType FuncType = Context.getFunctionType(
      InterpolationType, {SizeTType}, FunctionProtoType::ExtProtoInfo());

  CXXMethodDecl *InterpolationFunc = CXXMethodDecl::Create(
      Context, StructDecl, Loc,
      DeclarationNameInfo(Context.DeclarationNames.getIdentifier(
          &Context.Idents.get("interpolation")), Loc),
      FuncType,
      Context.getTrivialTypeSourceInfo(FuncType, Loc),
      SC_Static,
      /*UsesFPIntrin=*/false,
      /*isInline=*/true,
      ConstexprSpecKind::Consteval,
      Loc);

  InterpolationFunc->setImplicit(true);
  InterpolationFunc->setAccess(AS_public);

  ParmVarDecl *NParam = ParmVarDecl::Create(
      Context, InterpolationFunc, Loc, Loc,
      &Context.Idents.get("n"),
      SizeTType,
      Context.getTrivialTypeSourceInfo(SizeTType, Loc),
      SC_None, nullptr);
  NParam->setScopeInfo(0, 0);
  InterpolationFunc->setParams({NParam});

  DeclRefExpr *NRef = DeclRefExpr::Create(
      Context, NestedNameSpecifierLoc(), Loc, NParam, false,
      DeclarationNameInfo(NParam->getDeclName(), Loc),
      SizeTType, VK_LValue);

  ImplicitCastExpr *NValue = ImplicitCastExpr::Create(
      Context, SizeTType, CK_LValueToRValue, NRef,
      nullptr, VK_PRValue, FPOptionsOverride());

  SmallVector<Stmt*, 8> BodyStmts;
  for (size_t I = 0; I < NumInterpolations; ++I) {
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

    IntegerLiteral *IndexLit = CreateIntegerLit(CurIndex);
    IntegerLiteral *CountLit = CreateIntegerLit(NextIndex - CurIndex);

    Expr* FieldInits[] = {ExprToPtr, FmtToPtr, IndexLit, CountLit};

    InitListExpr *InterpolationInit = new (Context) InitListExpr(
        Context, Loc, FieldInits, Loc);
    InterpolationInit->setType(InterpolationType);

    ReturnStmt *Return = ReturnStmt::Create(Context, Loc, InterpolationInit, nullptr);

    IntegerLiteral *ILit = CreateIntegerLit(I);
    BinaryOperator *Cond = BinaryOperator::Create(
        Context, NValue, ILit, BO_EQ, Context.BoolTy, VK_PRValue, OK_Ordinary, Loc, FPOptionsOverride());

    IfStmt *If = IfStmt::Create(Context, Loc, IfStatementKind::Ordinary,
                                nullptr, nullptr, Cond, Loc, Loc, Return, Loc, nullptr);
    BodyStmts.push_back(If);
  }

  CXXNullPtrLiteralExpr *NullExpr1 = new (Context) CXXNullPtrLiteralExpr(CharConstPtrType, Loc);
  CXXNullPtrLiteralExpr *NullExpr2 = new (Context) CXXNullPtrLiteralExpr(CharConstPtrType, Loc);
  Expr* EmptyInits[] = {NullExpr1, NullExpr2, CreateIntegerLit(0), CreateIntegerLit(0)};
  InitListExpr *EmptyInit = new (Context) InitListExpr(Context, Loc, EmptyInits, Loc);
  EmptyInit->setType(InterpolationType);
  ReturnStmt *DefaultReturn = ReturnStmt::Create(Context, Loc, EmptyInit, nullptr);
  BodyStmts.push_back(DefaultReturn);

  InterpolationFunc->setBody(CompoundStmt::Create(Context, BodyStmts, FPOptionsOverride(), Loc, Loc));

  return InterpolationFunc;
}

static CXXMethodDecl *CreateNumInterpolationsFunction(Sema &S,
                                                      const TemplateStringAnnotation &Annotation,
                                                      CXXRecordDecl* StructDecl)
{
  ASTContext &Context = S.Context;
  SourceLocation Loc = StructDecl->getLocation();
  QualType SizeTType = Context.getSizeType();

  size_t NumInterpolations = Annotation.Interpolations.size();

  IntegerLiteral *NumLit = IntegerLiteral::Create(
      Context, llvm::APInt(Context.getTypeSize(SizeTType), NumInterpolations),
      SizeTType, Loc);

  QualType FuncType = Context.getFunctionType(
      SizeTType, {}, FunctionProtoType::ExtProtoInfo());

  CXXMethodDecl *NumFunc = CXXMethodDecl::Create(
      Context, StructDecl, Loc,
      DeclarationNameInfo(Context.DeclarationNames.getIdentifier(
          &Context.Idents.get("num_interpolations")), Loc),
      FuncType,
      Context.getTrivialTypeSourceInfo(FuncType, Loc),
      SC_Static,
      /*UsesFPIntrin=*/false,
      /*isInline=*/true,
      ConstexprSpecKind::Consteval,
      Loc);

  NumFunc->setImplicit(true);
  NumFunc->setAccess(AS_public);

  ReturnStmt *Return = ReturnStmt::Create(Context, Loc, NumLit, nullptr);
  NumFunc->setBody(CompoundStmt::Create(Context, {Return}, FPOptionsOverride(), Loc, Loc));

  return NumFunc;
}

ExprResult Sema::ActOnTemplateStringLiteral(SourceLocation Loc,
                                            const TemplateStringAnnotation& Annotation,
                                            ArrayRef<ExprResult> Exprs) {
  for (auto const& E : Exprs) {
    if (E.isInvalid())
      return ExprError();
  }

  CXXRecordDecl *StructDecl = CXXRecordDecl::Create(
      Context, TagTypeKind::Struct, CurContext, Loc, Loc,
      /*Id=*/nullptr);

  StructDecl->startDefinition();

  StructDecl->addDecl(CreateFmtFunction(*this, Annotation, StructDecl));
  StructDecl->addDecl(CreateStringFunction(*this, Annotation, StructDecl));
  StructDecl->addDecl(CreateInterpolationFunction(*this, Annotation, StructDecl));
  StructDecl->addDecl(CreateNumInterpolationsFunction(*this, Annotation, StructDecl));

  SmallVector<Decl*, 4> FieldDecls;
  for (size_t I = 0; I < Exprs.size(); ++I) {
    Expr *E = Exprs[I].get();

    QualType FieldType;
    if (E->isTypeDependent() || E->isValueDependent() ||
        E->getType()->isUndeducedAutoType()) {
      ParenExpr *PE = new (Context) ParenExpr(Loc, Loc, E);
      FieldType = Context.getDecltypeType(PE, Context.DependentTy);
    } else {
      FieldType = Context.getReferenceQualifiedType(E);
    }

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

  ActOnFields(/*S=*/nullptr, Loc, StructDecl, FieldDecls, Loc, Loc,
              ParsedAttributesView{});
  CheckCompletedCXXClass(/*S=*/nullptr, StructDecl);

  // Apparently not actually needed?
  // CurContext->addDecl(StructDecl);

  QualType StructType = Context.getRecordType(StructDecl);
  TypeSourceInfo *StructTSI = Context.getTrivialTypeSourceInfo(StructType, Loc);

  SmallVector<Expr*, 4> InitExprs;
  for (auto E : Exprs) {
    InitExprs.push_back(E.get());
  }

  InitListExpr *InitList = new (Context) InitListExpr(Context, Loc, InitExprs, Loc);
  InitList->setType(StructType);

  Expr *FunctionalCast = CXXFunctionalCastExpr::Create(
      Context, StructType, VK_PRValue, StructTSI, CK_NoOp,
      InitList, /*Path=*/nullptr, CurFPFeatureOverrides(), Loc, Loc);

  DeclStmt *StructDeclStmt = new (Context) DeclStmt(DeclGroupRef(StructDecl), Loc, Loc);

  Stmt *Stmts[] = {StructDeclStmt, FunctionalCast};
  CompoundStmt *Compound = CompoundStmt::Create(Context, Stmts, FPOptionsOverride(), Loc, Loc);

  unsigned TemplateDepth = 0;
  for (DeclContext *DC = CurContext; DC; DC = DC->getParent()) {
    if (auto *ESD = dyn_cast<ExpansionStmtDecl>(DC)) {
      TemplateDepth = ESD->getTemplateDepth();
      break;
    }
    if (DC->isFileContext())
      break;
  }

  Expr *Result = new (Context) StmtExpr(Compound, StructType, Loc, Loc, TemplateDepth);
  return MaybeBindToTemporary(Result);
}
