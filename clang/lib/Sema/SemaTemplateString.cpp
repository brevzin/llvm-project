//===--- SemaTemplateString.cpp - Template String Literal Handling -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements semantic analysis for template string literals (P3951).
// A template string literal t"..." produces a prvalue of a unique struct with
//
//   static consteval char const *fmt();
//   static consteval char const *string(size_t n);
//   static consteval std::interpolation interpolation(size_t n);
//   static consteval size_t num_interpolations();
//   constexpr auto exprs() const -> S const &;
//   /* one data member _N per interpolated expression */
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/Sema.h"

using namespace clang;

namespace {

/// Synthesizes the members of the struct generated for a template string.
class TemplateStringStructBuilder {
  ASTContext &Context;
  CXXRecordDecl *Struct;
  SourceLocation Loc;
  QualType CharConstPtrTy;
  QualType SizeTy;

  IntegerLiteral *makeSizeT(uint64_t V) {
    return IntegerLiteral::Create(
        Context, llvm::APInt(Context.getTypeSize(SizeTy), V), SizeTy, Loc);
  }

  /// A string literal decayed to 'char const *'.
  Expr *makeCString(StringRef Str) {
    QualType ArrTy = Context.getConstantArrayType(
        Context.CharTy.withConst(), llvm::APInt(32, Str.size() + 1), nullptr,
        ArraySizeModifier::Normal, 0);
    StringLiteral *Lit = StringLiteral::Create(
        Context, Str, StringLiteralKind::Ordinary, false, ArrTy, {Loc});
    return ImplicitCastExpr::Create(Context, CharConstPtrTy,
                                    CK_ArrayToPointerDecay, Lit, nullptr,
                                    VK_PRValue, FPOptionsOverride());
  }

  ReturnStmt *makeReturn(Expr *E) {
    return ReturnStmt::Create(Context, Loc, E, nullptr);
  }

  /// The value of a parameter, as a prvalue.
  Expr *loadParam(ParmVarDecl *P) {
    DeclRefExpr *Ref = DeclRefExpr::Create(
        Context, NestedNameSpecifierLoc(), Loc, P, false,
        DeclarationNameInfo(P->getDeclName(), Loc), P->getType(), VK_LValue);
    return ImplicitCastExpr::Create(Context, P->getType(), CK_LValueToRValue,
                                    Ref, nullptr, VK_PRValue,
                                    FPOptionsOverride());
  }

  /// Declares and adds a public inline member function; the caller supplies
  /// the body.
  CXXMethodDecl *declareMethod(StringRef Name, QualType RetTy,
                               ArrayRef<QualType> ParamTys, StorageClass SC,
                               ConstexprSpecKind CSK, bool IsConst = false) {
    FunctionProtoType::ExtProtoInfo EPI;
    if (IsConst)
      EPI.TypeQuals.addConst();
    QualType FnTy = Context.getFunctionType(RetTy, ParamTys, EPI);

    auto *MD = CXXMethodDecl::Create(
        Context, Struct, Loc,
        DeclarationNameInfo(&Context.Idents.get(Name), Loc), FnTy,
        Context.getTrivialTypeSourceInfo(FnTy, Loc), SC,
        /*UsesFPIntrin=*/false, /*isInline=*/true, CSK, Loc);
    MD->setImplicit();
    MD->setAccess(AS_public);

    SmallVector<ParmVarDecl *, 1> Params;
    for (auto [I, ParamTy] : llvm::enumerate(ParamTys)) {
      auto *P = ParmVarDecl::Create(Context, MD, Loc, Loc,
                                    &Context.Idents.get("n"), ParamTy,
                                    Context.getTrivialTypeSourceInfo(ParamTy, Loc),
                                    SC_None, nullptr);
      P->setScopeInfo(0, I);
      Params.push_back(P);
    }
    MD->setParams(Params);

    Struct->addDecl(MD);
    return MD;
  }

  void setBody(CXXMethodDecl *MD, ArrayRef<Stmt *> Body) {
    MD->setBody(CompoundStmt::Create(Context, Body, FPOptionsOverride(), Loc,
                                     Loc));
  }

  /// 'static consteval Ret name(size_t n)' returning Values[n], written as a
  /// chain of 'if (n == I) return Values[I];'. An out-of-range index falls
  /// off the end of the function, which makes the (necessarily constant)
  /// evaluation fail with a diagnostic rather than silently yielding garbage.
  void addIndexedLookup(StringRef Name, QualType RetTy,
                        ArrayRef<Expr *> Values) {
    CXXMethodDecl *MD = declareMethod(Name, RetTy, {SizeTy}, SC_Static,
                                      ConstexprSpecKind::Consteval);
    SmallVector<Stmt *, 8> Body;
    for (auto [I, V] : llvm::enumerate(Values)) {
      Expr *Cond = BinaryOperator::Create(
          Context, loadParam(MD->getParamDecl(0)), makeSizeT(I), BO_EQ,
          Context.BoolTy, VK_PRValue, OK_Ordinary, Loc, FPOptionsOverride());
      Body.push_back(IfStmt::Create(Context, Loc, IfStatementKind::Ordinary,
                                    nullptr, nullptr, Cond, Loc, Loc,
                                    makeReturn(V), Loc, nullptr));
    }
    setBody(MD, Body);
  }

public:
  TemplateStringStructBuilder(ASTContext &Context, CXXRecordDecl *Struct,
                              SourceLocation Loc)
      : Context(Context), Struct(Struct), Loc(Loc),
        CharConstPtrTy(Context.getPointerType(Context.CharTy.withConst())),
        SizeTy(Context.getSizeType()) {}

  /// static consteval char const *fmt();
  void addFmt(const TemplateStringLiteralData &Data) {
    CXXMethodDecl *MD = declareMethod("fmt", CharConstPtrTy, {}, SC_Static,
                                      ConstexprSpecKind::Consteval);
    setBody(MD, {makeReturn(makeCString(Data.FormatString))});
  }

  /// static consteval size_t num_interpolations();
  void addNumInterpolations(const TemplateStringLiteralData &Data) {
    CXXMethodDecl *MD = declareMethod("num_interpolations", SizeTy, {},
                                      SC_Static, ConstexprSpecKind::Consteval);
    setBody(MD, {makeReturn(makeSizeT(Data.Interpolations.size()))});
  }

  /// static consteval char const *string(size_t n);
  void addString(const TemplateStringLiteralData &Data) {
    SmallVector<Expr *, 8> Values;
    for (const std::string &Piece : Data.StringPieces)
      Values.push_back(makeCString(Piece));
    addIndexedLookup("string", CharConstPtrTy, Values);
  }

  /// static consteval std::interpolation interpolation(size_t n);
  void addInterpolation(const TemplateStringLiteralData &Data,
                        QualType InterpTy) {
    SmallVector<Expr *, 8> Values;
    for (const auto &Interp : Data.Interpolations) {
      Expr *Inits[] = {makeCString(Interp.ExpressionText),
                       makeCString(Interp.FormatSpecifier),
                       makeSizeT(Interp.ExpressionIndex),
                       makeSizeT(Interp.ExpressionCount)};
      auto *Init = new (Context)
          InitListExpr(Context, Loc, Inits, Loc, /*isExplicit=*/false);
      Init->setType(InterpTy);
      Values.push_back(Init);
    }
    addIndexedLookup("interpolation", InterpTy, Values);
  }

  /// constexpr auto exprs() const -> S const & { return *this; }
  void addExprs() {
    QualType ConstStructTy = Context.getCanonicalTagType(Struct).withConst();
    CXXMethodDecl *MD = declareMethod(
        "exprs", Context.getLValueReferenceType(ConstStructTy), {}, SC_None,
        ConstexprSpecKind::Constexpr, /*IsConst=*/true);

    CXXThisExpr *This = CXXThisExpr::Create(
        Context, Loc, Context.getPointerType(ConstStructTy),
        /*IsImplicit=*/false);
    UnaryOperator *Deref = UnaryOperator::Create(
        Context, This, UO_Deref, ConstStructTy, VK_LValue, OK_Ordinary, Loc,
        /*CanOverflow=*/false, FPOptionsOverride());
    setBody(MD, {makeReturn(Deref)});
  }

  /// One public data member _I per interpolated expression, typed as
  /// decltype((expr)) so that lvalues are captured by reference.
  FieldDecl *addField(unsigned I, Expr *E) {
    QualType FieldTy;
    if (E->isTypeDependent() || E->isValueDependent() ||
        E->getType()->isUndeducedAutoType()) {
      auto *PE = new (Context) ParenExpr(Loc, Loc, E);
      FieldTy = Context.getDecltypeType(PE, Context.DependentTy);
    } else {
      FieldTy = Context.getReferenceQualifiedType(E);
    }

    SmallString<16> Name("_");
    Name += std::to_string(I);
    FieldDecl *Field = FieldDecl::Create(
        Context, Struct, Loc, Loc, &Context.Idents.get(Name), FieldTy,
        Context.getTrivialTypeSourceInfo(FieldTy, Loc), /*BitWidth=*/nullptr,
        /*Mutable=*/false, ICIS_NoInit);
    Field->setAccess(AS_public);
    Struct->addDecl(Field);
    return Field;
  }
};

} // namespace

QualType Sema::CheckStdInterpolation(SourceLocation Loc) {
  if (!StdInterpolation) {
    NamespaceDecl *Std = getStdNamespace();
    if (!Std) {
      Diag(Loc, diag::err_implied_std_interpolation_not_found);
      return QualType();
    }
    LookupResult Result(*this, &Context.Idents.get("interpolation"), Loc,
                        LookupOrdinaryName);
    if (!LookupQualifiedName(Result, Std)) {
      Diag(Loc, diag::err_implied_std_interpolation_not_found);
      return QualType();
    }
    auto *RD = Result.getAsSingle<CXXRecordDecl>();
    if (!RD) {
      Diag(Loc, diag::err_malformed_std_interpolation);
      return QualType();
    }
    if (!RD->hasDefinition()) {
      Diag(Loc, diag::err_malformed_std_interpolation);
      return QualType();
    }
    RD = RD->getDefinition();

    // The generated interpolation() members aggregate-initialize this type
    // positionally, so its shape is part of the contract.
    QualType CharConstPtrTy = Context.getPointerType(Context.CharTy.withConst());
    QualType Expected[] = {CharConstPtrTy, CharConstPtrTy,
                           Context.getSizeType(), Context.getSizeType()};
    unsigned I = 0;
    bool Valid = RD->getNumBases() == 0;
    for (FieldDecl *Field : RD->fields()) {
      if (I == 4 || Field->isBitField() ||
          !Context.hasSameUnqualifiedType(Field->getType(), Expected[I])) {
        Valid = false;
        break;
      }
      ++I;
    }
    if (!Valid || I != 4) {
      Diag(Loc, diag::err_malformed_std_interpolation);
      return QualType();
    }
    StdInterpolation = RD;
  }
  return Context.getCanonicalTagType(StdInterpolation);
}

ExprResult Sema::BuildTemplateStringLiteral(SourceRange Range,
                                            TemplateStringLiteralData *Data,
                                            ArrayRef<Expr *> Exprs) {
  SourceLocation Loc = Range.getBegin();

  QualType InterpTy = CheckStdInterpolation(Loc);
  if (InterpTy.isNull())
    return ExprError();

  // Resolve placeholders and settle on what each member holds. A bit-field
  // cannot be bound to a reference, so it is captured by value, as it would
  // be when passed to std::format directly.
  SmallVector<Expr *, 8> Inits;
  for (Expr *E : Exprs) {
    if (!E->isTypeDependent()) {
      ExprResult Res = CheckPlaceholderExpr(E);
      if (Res.isInvalid())
        return ExprError();
      E = Res.get();

      if (E->getType()->isVoidType()) {
        Diag(E->getExprLoc(), diag::err_template_string_void_expr)
            << E->getSourceRange();
        return ExprError();
      }
      if (E->refersToBitField()) {
        Res = DefaultLvalueConversion(E);
        if (Res.isInvalid())
          return ExprError();
        E = Res.get();
      }
    }
    Inits.push_back(E);
  }

  CXXRecordDecl *Struct = CXXRecordDecl::Create(
      Context, TagTypeKind::Struct, CurContext, Loc, Loc, /*Id=*/nullptr);
  Struct->setImplicit();
  Struct->startDefinition();
  CurContext->addDecl(Struct);

  TemplateStringStructBuilder Builder(Context, Struct, Loc);
  Builder.addFmt(*Data);
  Builder.addString(*Data);
  Builder.addInterpolation(*Data, InterpTy);
  Builder.addNumInterpolations(*Data);
  Builder.addExprs();

  SmallVector<Decl *, 8> Fields;
  for (auto [I, E] : llvm::enumerate(Inits))
    Fields.push_back(Builder.addField(I, E));

  ActOnFields(/*S=*/nullptr, Loc, Struct, Fields, Loc, Loc,
              ParsedAttributesView{});
  CheckCompletedCXXClass(/*S=*/nullptr, Struct);
  if (Struct->isInvalidDecl())
    return ExprError();

  return MaybeBindToTemporary(
      TemplateStringLiteralExpr::Create(Context, Struct, Data, Inits, Range));
}

ExprResult Sema::ActOnTemplateStringLiteral(SourceRange Range,
                                            TemplateStringLiteralData &&Data,
                                            ArrayRef<Expr *> Exprs) {
  auto *StoredData = new (Context) TemplateStringLiteralData(std::move(Data));
  Context.addDestruction(StoredData);
  return BuildTemplateStringLiteral(Range, StoredData, Exprs);
}

ExprResult Sema::ActOnTemplateStringUDL(Expr *TemplateStringExpr,
                                        IdentifierInfo *UDSuffix,
                                        SourceLocation UDSuffixLoc, Scope *S) {
  DeclarationName OpName =
      Context.DeclarationNames.getCXXLiteralOperatorName(UDSuffix);
  DeclarationNameInfo OpNameInfo(OpName, UDSuffixLoc);
  OpNameInfo.setCXXLiteralOperatorNameLoc(UDSuffixLoc);

  LookupResult R(*this, OpName, UDSuffixLoc, LookupOrdinaryName);
  LookupName(R, S);

  Expr *Args[] = {TemplateStringExpr};
  OverloadCandidateSet CandidateSet(UDSuffixLoc,
                                    OverloadCandidateSet::CSK_Normal);
  AddFunctionCandidates(R.asUnresolvedSet(), Args, CandidateSet);

  OverloadCandidateSet::iterator Best;
  switch (CandidateSet.BestViableFunction(*this, UDSuffixLoc, Best)) {
  case OR_Success:
  case OR_Deleted:
    break;
  case OR_No_Viable_Function:
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(UDSuffixLoc,
                            PDiag(diag::err_ovl_no_viable_function_in_call)
                                << R.getLookupName()),
        *this, OCD_AllCandidates, Args);
    return ExprError();
  case OR_Ambiguous:
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(UDSuffixLoc, PDiag(diag::err_ovl_ambiguous_call)
                                             << R.getLookupName()),
        *this, OCD_AmbiguousCandidates, Args);
    return ExprError();
  }

  FunctionDecl *FD = Best->Function;
  if (DiagnoseUseOfDecl(Best->FoundDecl, UDSuffixLoc))
    return ExprError();
  if (Best->FoundDecl != FD && DiagnoseUseOfDecl(FD, UDSuffixLoc))
    return ExprError();

  ExprResult Fn = BuildDeclRefExpr(FD, FD->getType(), VK_LValue, UDSuffixLoc);
  if (Fn.isInvalid())
    return ExprError();

  // FIXME: LookupLiteralOperator does not know about template string
  // operands, so this builds a plain CallExpr rather than a UserDefinedLiteral.
  return BuildResolvedCallExpr(Fn.get(), FD, UDSuffixLoc, Args, UDSuffixLoc);
}
