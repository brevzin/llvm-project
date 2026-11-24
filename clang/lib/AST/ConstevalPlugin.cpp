//===--- ConstevalPlugin.cpp - Native consteval plugin runtime --*- C++ -*-===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ConstevalPlugin.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/MetaActions.h"
#include "clang/AST/Metafunction.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Reflection.h"
#include "clang/Basic/DiagnosticAST.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <csetjmp>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace clang;

namespace {

/// Active host state for the currently executing consteval plugin call.
struct ActiveHostState {
  ASTContext &Ctx;
  MetaActions &Meta;
  SourceLocation CallLoc;
  Decl *ContainingDecl;
  bool AllowInjection;
  Expr::EvalStatus *EvalStatus;
  std::string &ErrorTag;
  std::string &ErrorMsg;
  bool Failed = false;
  std::jmp_buf ErrorJump;
  bool HasErrorJump = false;

  // Handle arenas: handle 0 is null reflection / empty token sequence.
  // Handle i (i >= 1) indexes into slot (i - 1).
  std::vector<APValue> Reflections;
  std::vector<TokenSequenceData> TokenSeqs;
  llvm::BumpPtrAllocator ScratchAlloc;

  uint64_t internReflection(const APValue &V) {
    if (V.isReflection() && V.getReflectionKind() == ReflectionKind::Null)
      return 0;
    Reflections.push_back(V);
    return Reflections.size();
  }

  APValue lookupReflection(uint64_t Handle) const {
    if (Handle == 0 || Handle > Reflections.size())
      return APValue(ReflectionKind::Null, nullptr);
    return Reflections[Handle - 1];
  }

  uint64_t internTokenSeq(TokenSequenceData TSD) {
    if (TSD.empty())
      return 0;
    TokenSeqs.push_back(TSD);
    return TokenSeqs.size();
  }

  TokenSequenceData lookupTokenSeq(uint64_t Handle) const {
    if (Handle == 0 || Handle > TokenSeqs.size())
      return CreateEmptyTokenSequenceData(Ctx);
    return TokenSeqs[Handle - 1];
  }

  const char *copyString(llvm::StringRef S) {
    char *Buf = Ctx.Allocate<char>(S.size() + 1);
    if (!S.empty())
      std::memcpy(Buf, S.data(), S.size());
    Buf[S.size()] = '\0';
    return Buf;
  }
};

thread_local ActiveHostState *CurrentHost = nullptr;

/// Helper: wrap an APValue in a ConstantExpr so Metafunction::evaluate can read
/// it through its standard Expr* interface.
static Expr *makeConstArgExpr(ASTContext &C, SourceLocation Loc,
                              const APValue &V, QualType T) {
  auto *OVE = new (C) OpaqueValueExpr(Loc, T, VK_PRValue);
  return ConstantExpr::Create(C, OVE, V);
}

static bool hostCallMetafn(unsigned MetaFnID, unsigned ResultKind,
                           const ConstevalArg *Args, size_t NumArgs,
                           ConstevalArg *Out) {
  ActiveHostState *H = CurrentHost;
  if (!H)
    return false;

  const Metafunction *Metafn = nullptr;
  if (Metafunction::Lookup(MetaFnID, Metafn) || !Metafn) {
    H->Failed = true;
    H->ErrorMsg = "unknown metafunction ID in consteval plugin";
    return false;
  }

  ASTContext &C = H->Ctx;
  SourceLocation Loc = H->CallLoc;
  SourceRange Range(Loc, Loc);

  auto Evaluator = [&](APValue &Result, const Expr *E,
                       unsigned /*ConvertToRValue*/) -> bool {
    if (const auto *CE = dyn_cast<ConstantExpr>(E)) {
      Result = CE->getAPValueResult();
      return true;
    }
    if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
      if (const auto *BaseCE = dyn_cast<ConstantExpr>(ASE->getBase())) {
        if (const auto *IdxLit = dyn_cast<IntegerLiteral>(ASE->getIdx())) {
          uint64_t BaseAddr = BaseCE->getAPValueResult().getInt().getZExtValue();
          uint64_t Idx = IdxLit->getValue().getZExtValue();
          size_t Stride = C.getTypeSizeInChars(C.MetaInfoTy).getQuantity();
          uint64_t Handle = 0;
          const auto *Ptr = reinterpret_cast<const uint8_t *>(
              static_cast<uintptr_t>(BaseAddr));
          std::memcpy(&Handle, Ptr + Idx * Stride,
                      std::min(Stride, sizeof(Handle)));
          Result = H->lookupReflection(Handle);
          return true;
        }
      }
    }
    Expr::EvalResult ER;
    if (E->EvaluateAsRValue(ER, C)) {
      Result = std::move(ER.Val);
      return true;
    }
    return false;
  };

  std::vector<PartialDiagnosticAt> Diags;
  auto Diagnoser = [&](SourceLocation DLoc,
                       diag::kind DiagID) -> PartialDiagnostic & {
    auto &D = Diags.emplace_back(
        std::piecewise_construct, std::make_tuple(DLoc),
        std::make_tuple(DiagID, std::ref(C.getDiagAllocator())));
    return D.second;
  };

  // Special case 1: members_gather(info* dst, r, kind, scope, designating, cap)
  if (Metafn->isMemberGather() && NumArgs >= 6) {
    SmallVector<Expr *, 6> ArgExprs;
    ArgExprs.push_back(makeConstArgExpr(C, Loc, APValue(), C.VoidPtrTy));
    ArgExprs.push_back(makeConstArgExpr(
        C, Loc, H->lookupReflection(Args[1].Val), C.MetaInfoTy));
    ArgExprs.push_back(makeConstArgExpr(
        C, Loc, APValue(C.MakeIntValue(Args[2].Val, C.getSizeType())),
        C.getSizeType()));
    ArgExprs.push_back(makeConstArgExpr(
        C, Loc, H->lookupReflection(Args[3].Val), C.MetaInfoTy));
    ArgExprs.push_back(makeConstArgExpr(
        C, Loc, H->lookupReflection(Args[4].Val), C.MetaInfoTy));
    ArgExprs.push_back(makeConstArgExpr(
        C, Loc, APValue(C.MakeIntValue(Args[5].Val, C.getSizeType())),
        C.getSizeType()));

    APValue Gathered;
    if (Metafn->evaluate(Gathered, C, H->Meta, Evaluator, Diagnoser,
                         H->AllowInjection, C.getSizeType(), Range, ArgExprs,
                         H->ContainingDecl)) {
      H->Failed = true;
      return false;
    }
    unsigned N = Gathered.getArraySize();
    auto *Dst = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(Args[0].Val));
    if (Dst) {
      size_t Stride = C.getTypeSizeInChars(C.MetaInfoTy).getQuantity();
      for (unsigned I = 0; I != N; ++I) {
        uint8_t *Slot = Dst + I * Stride;
        std::memset(Slot, 0, Stride);
        uint64_t Handle = H->internReflection(Gathered.getArrayInitializedElt(I));
        std::memcpy(Slot, &Handle, std::min(Stride, sizeof(Handle)));
      }
    }
    Out->Kind = CEK_Int;
    Out->Val = N;
    return true;
  }

  // Special case 2: tokens_join(const token* ptr, size_t n)
  if (Metafn->getTokenArrayOp() == Metafunction::TokenArrayOp::Join &&
      NumArgs >= 2) {
    auto *Ptr =
        reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(Args[0].Val));
    size_t N = static_cast<size_t>(Args[1].Val);
    size_t Stride = C.getTypeSizeInChars(C.MetaTokenTy).getQuantity();
    SmallVector<Token, 32> Toks;
    for (size_t I = 0; I < N; ++I) {
      uint64_t Handle = 0;
      std::memcpy(&Handle, Ptr + I * Stride, std::min(Stride, sizeof(Handle)));
      TokenSequenceData TSD = H->lookupTokenSeq(Handle);
      Toks.append(TSD.begin(), TSD.end());
    }
    TokenSequenceData Joined = CreateTokenSequenceData(C, Toks);
    Out->Kind = CEK_TokenSequence;
    Out->Val = H->internTokenSeq(Joined);
    return true;
  }

  // General case: build ConstantExpr arguments from ConstevalArg[].
  SmallVector<Expr *, 8> ArgExprs;
  ArgExprs.reserve(NumArgs);
  for (size_t I = 0; I < NumArgs; ++I) {
    const ConstevalArg &A = Args[I];
    switch (A.Kind) {
    case CEK_Bool:
      ArgExprs.push_back(makeConstArgExpr(
          C, Loc, APValue(C.MakeIntValue(A.Val != 0, C.BoolTy)), C.BoolTy));
      break;
    case CEK_Int: {
      QualType ITy = C.getSizeType();
      // For reflect_constant(T r), use the reflected type from Args[0].
      if (MetaFnID == Metafunction::getReflectResultID() && I == 1 &&
          NumArgs >= 1 && Args[0].Kind == CEK_MetaInfo) {
        APValue TyRefl = H->lookupReflection(Args[0].Val);
        if (TyRefl.isReflectedType() &&
            TyRefl.getReflectedType()->isIntegralOrEnumerationType())
          ITy = TyRefl.getReflectedType();
      }
      ArgExprs.push_back(
          makeConstArgExpr(C, Loc, APValue(C.MakeIntValue(A.Val, ITy)), ITy));
      break;
    }
    case CEK_MetaInfo:
      ArgExprs.push_back(makeConstArgExpr(C, Loc, H->lookupReflection(A.Val),
                                          C.MetaInfoTy));
      break;
    case CEK_TokenSequence:
    case CEK_MetaToken:
      ArgExprs.push_back(makeConstArgExpr(
          C, Loc, APValue(H->lookupTokenSeq(A.Val)), C.TokenSequenceTy));
      break;
    case CEK_CharPtr: {
      const char *S =
          reinterpret_cast<const char *>(static_cast<uintptr_t>(A.Val));
      llvm::StringRef SR(S ? S : "", S ? (A.Len ? A.Len : std::strlen(S)) : 0);
      auto *SL = StringLiteral::Create(
          C, SR, StringLiteralKind::Ordinary, /*Pascal=*/false,
          C.getStringLiteralArrayType(C.CharTy, SR.size()), Loc);
      APValue LV(SL, CharUnits::Zero(), APValue::NoLValuePath(),
                 /*CallIndex=*/0);
      ArgExprs.push_back(
          makeConstArgExpr(C, Loc, LV, C.getPointerType(C.CharTy.withConst())));
      break;
    }
    case CEK_MetaInfoPtr:
      ArgExprs.push_back(makeConstArgExpr(
          C, Loc, APValue(C.MakeIntValue(A.Val, C.getSizeType())),
          C.getPointerType(C.MetaInfoTy.withConst())));
      break;
    default:
      ArgExprs.push_back(makeConstArgExpr(C, Loc, APValue(), C.VoidPtrTy));
      break;
    }
  }

  QualType ResTy;
  switch (Metafn->getResultKind()) {
  case Metafunction::MFRK_spliceFromArg:
    if (NumArgs >= 1 && Args[0].Kind == CEK_MetaInfo) {
      APValue TyRefl = H->lookupReflection(Args[0].Val);
      if (TyRefl.isReflectedType())
        ResTy = TyRefl.getReflectedType();
    }
    break;
  case Metafunction::MFRK_bool:
    ResTy = C.BoolTy;
    break;
  case Metafunction::MFRK_metaInfo:
    ResTy = C.MetaInfoTy;
    break;
  case Metafunction::MFRK_tokenSequence:
    ResTy = C.TokenSequenceTy;
    break;
  case Metafunction::MFRK_token:
    ResTy = C.MetaTokenTy;
    break;
  case Metafunction::MFRK_tokenPtr:
    ResTy = C.getPointerType(C.MetaTokenTy.withConst());
    break;
  case Metafunction::MFRK_sizeT:
    ResTy = C.getSizeType();
    break;
  case Metafunction::MFRK_charPtr:
    ResTy = C.getPointerType(C.CharTy.withConst());
    break;
  default:
    ResTy = C.VoidTy;
    break;
  }

  APValue ResVal;
  if (Metafn->evaluate(ResVal, C, H->Meta, Evaluator, Diagnoser,
                       H->AllowInjection, ResTy, Range, ArgExprs,
                       H->ContainingDecl)) {
    H->Failed = true;
    return false;
  }

  // Convert APValue result back to ConstevalArg.
  if (ResVal.isInt()) {
    Out->Kind = (Metafn->getResultKind() == Metafunction::MFRK_bool)
                    ? CEK_Bool
                    : CEK_Int;
    Out->Val = ResVal.getInt().getZExtValue();
  } else if (ResVal.isReflection()) {
    Out->Kind = CEK_MetaInfo;
    Out->Val = H->internReflection(ResVal);
  } else if (ResVal.isTokenSequence()) {
    Out->Kind = CEK_TokenSequence;
    Out->Val = H->internTokenSeq(ResVal.getTokenSequence());
  } else if (ResVal.isLValue() && ResTy->isPointerType() &&
             ResTy->getPointeeType()->isAnyCharacterType()) {
    const Expr *BaseE = ResVal.getLValueBase().dyn_cast<const Expr *>();
    if (const auto *SL = dyn_cast_or_null<StringLiteral>(BaseE)) {
      const char *Copied = H->copyString(SL->getString());
      Out->Kind = CEK_CharPtr;
      Out->Val = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Copied));
      Out->Len = SL->getLength();
    } else {
      Out->Kind = CEK_CharPtr;
      Out->Val = 0;
      Out->Len = 0;
    }
  } else if (ResVal.isLValue() &&
             Metafn->getResultKind() == Metafunction::MFRK_tokenPtr) {
    // Materialize a native array of token handles in ScratchAlloc.
    const ValueDecl *VD = ResVal.getLValueBase().dyn_cast<const ValueDecl *>();
    if (const TokenSequenceData *TSD = getMaterializedTokens(VD)) {
      size_t Stride = C.getTypeSizeInChars(C.MetaTokenTy).getQuantity();
      uint8_t *Arr = H->ScratchAlloc.Allocate<uint8_t>(TSD->size() * Stride);
      std::memset(Arr, 0, TSD->size() * Stride);
      for (size_t I = 0; I < TSD->size(); ++I) {
        uint64_t Handle = H->internTokenSeq(TokenSequenceData(TSD->data() + I, 1));
        std::memcpy(Arr + I * Stride, &Handle, std::min(Stride, sizeof(Handle)));
      }
      Out->Kind = CEK_TokenPtr;
      Out->Val = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Arr));
      Out->Len = TSD->size();
    } else {
      Out->Kind = CEK_TokenPtr;
      Out->Val = 0;
      Out->Len = 0;
    }
  } else {
    Out->Kind = CEK_Void;
    Out->Val = 0;
  }
  return true;
}

static uint64_t hostMakeTokenSeq(const ConstevalTokenDesc *Toks, size_t NumToks,
                                 const ConstevalArg *Splices,
                                 size_t NumSplices) {
  ActiveHostState *H = CurrentHost;
  if (!H)
    return 0;

  ASTContext &C = H->Ctx;
  SourceLocation Loc = H->CallLoc;
  SmallVector<Token, 32> Out;
  Out.reserve(NumToks);

  for (size_t I = 0; I < NumToks; ++I) {
    const ConstevalTokenDesc &D = Toks[I];
    tok::TokenKind K = static_cast<tok::TokenKind>(D.TokKind);
    if (K == tok::annot_token_seq_expr) {
      if (D.SpliceIdx < NumSplices) {
        const ConstevalArg &S = Splices[D.SpliceIdx];
        if (S.Kind == CEK_TokenSequence || S.Kind == CEK_MetaToken) {
          TokenSequenceData TSD = H->lookupTokenSeq(S.Val);
          Out.append(TSD.begin(), TSD.end());
        } else if (S.Kind == CEK_MetaInfo) {
          APValue RV = H->lookupReflection(S.Val);
          Token T;
          T.startToken();
          T.setLocation(Loc);
          if (RV.isReflectedType()) {
            QualType QT = stripDeducedTypeSugar(C, RV.getReflectedType());
            T.setKind(tok::annot_typename);
            T.setAnnotationEndLoc(Loc);
            T.setAnnotationValue(QT.getAsOpaquePtr());
          } else {
            auto *OVE = new (C) OpaqueValueExpr(Loc, C.MetaInfoTy, VK_PRValue);
            Expr *CE = ConstantExpr::Create(C, OVE, RV);
            T.setKind(tok::annot_token_seq_expr);
            T.setAnnotationEndLoc(Loc);
            T.setAnnotationValue(CE);
          }
          Out.push_back(T);
        } else if (S.Kind == CEK_Int || S.Kind == CEK_Bool) {
          std::string Num = std::to_string(S.Val);
          const char *Buf = H->copyString(Num);
          Token T;
          T.startToken();
          T.setKind(tok::numeric_constant);
          T.setLocation(Loc);
          T.setLiteralData(Buf);
          T.setLength(Num.size());
          Out.push_back(T);
        }
      }
      continue;
    }

    Token T;
    T.startToken();
    T.setKind(K);
    T.setLocation(Loc);
    if (D.Text && D.TextLen > 0) {
      llvm::StringRef Text(D.Text, D.TextLen);
      if (K == tok::identifier || tok::getKeywordSpelling(K) != nullptr) {
        T.setIdentifierInfo(&C.Idents.get(Text));
        T.setLength(D.TextLen);
      } else {
        const char *Buf = H->copyString(Text);
        T.setLiteralData(Buf);
        T.setLength(D.TextLen);
      }
    } else if (const char *Spell = tok::getPunctuatorSpelling(K)) {
      T.setLength(std::strlen(Spell));
    } else if (const char *Spell = tok::getKeywordSpelling(K)) {
      T.setIdentifierInfo(&C.Idents.get(Spell));
      T.setLength(std::strlen(Spell));
    }
    Out.push_back(T);
  }

  return H->internTokenSeq(CreateTokenSequenceData(C, Out));
}

static uint64_t hostReflectBuiltin(unsigned EncodedKind) {
  ActiveHostState *H = CurrentHost;
  if (!H)
    return 0;
  ASTContext &C = H->Ctx;
  bool IsPtr = (EncodedKind >> 16) & 1;
  bool IsConst = (EncodedKind >> 15) & 1;
  unsigned BaseKind = EncodedKind & 0x7FFF;
  QualType QT;
  switch (static_cast<BuiltinType::Kind>(BaseKind)) {
  case BuiltinType::Void:      QT = C.VoidTy; break;
  case BuiltinType::Bool:      QT = C.BoolTy; break;
  case BuiltinType::Char_S:
  case BuiltinType::Char_U:    QT = C.CharTy; break;
  case BuiltinType::Char8:     QT = C.Char8Ty; break;
  case BuiltinType::Int:       QT = C.IntTy; break;
  case BuiltinType::UInt:      QT = C.UnsignedIntTy; break;
  case BuiltinType::Long:      QT = C.LongTy; break;
  case BuiltinType::ULong:     QT = C.UnsignedLongTy; break;
  case BuiltinType::LongLong:  QT = C.LongLongTy; break;
  case BuiltinType::ULongLong: QT = C.UnsignedLongLongTy; break;
  case BuiltinType::Float:     QT = C.FloatTy; break;
  case BuiltinType::Double:    QT = C.DoubleTy; break;
  default:                     return 0;
  }
  if (IsConst)
    QT = QT.withConst();
  if (IsPtr)
    QT = C.getPointerType(QT);
  return H->internReflection(APValue(ReflectionKind::Type, QT.getAsOpaquePtr()));
}

static NamedDecl *lookupInDC(ASTContext &C, DeclContext *DC,
                             IdentifierInfo &II) {
  auto Res = DC->lookup(&II);
  if (!Res.empty())
    return Res.front();
  // Also search inline namespaces (e.g. std::__1).
  for (Decl *D : DC->decls()) {
    if (auto *NS = dyn_cast<NamespaceDecl>(D); NS && NS->isInlineNamespace()) {
      if (NamedDecl *Found = lookupInDC(C, NS, II))
        return Found;
    }
  }
  return nullptr;
}

static uint64_t hostReflectNamed(const char *QualName, size_t Len) {
  ActiveHostState *H = CurrentHost;
  if (!H || !QualName || Len == 0)
    return 0;
  ASTContext &C = H->Ctx;
  llvm::StringRef Full(QualName, Len);
  if (Full.starts_with("::"))
    Full = Full.drop_front(2);

  DeclContext *DC = C.getTranslationUnitDecl();
  NamedDecl *Found = nullptr;
  while (!Full.empty()) {
    auto [Part, Rest] = Full.split("::");
    Full = Rest;
    IdentifierInfo &II = C.Idents.get(Part);
    Found = lookupInDC(C, DC, II);
    if (!Found) {
      llvm::StringRef QN(QualName, Len);
      // Unfound local sentinel class (e.g. std::meta::__range_of_infos::sentinel):
      if (QN.contains_insensitive("sentinel")) {
        return H->internReflection(
            APValue(ReflectionKind::Type, C.VoidTy.getAsOpaquePtr()));
      }
      // <meta>'s internal __wrap_workaround<T>{ using type = T; }: fall back
      // to std::type_identity if the consumer didn't #include <meta>.
      if (QN.ends_with("__wrap_workaround")) {
        return hostReflectNamed("std::type_identity", 18);
      }
      return 0;
    }
    if (!Full.empty()) {
      DC = dyn_cast<DeclContext>(Found);
      if (!DC)
        return 0;
    }
  }
  if (!Found)
    return 0;
  if (auto *TD = dyn_cast<TemplateDecl>(Found))
    return H->internReflection(
        APValue(ReflectionKind::Template, TemplateName(TD).getAsVoidPointer()));
  if (auto *TyD = dyn_cast<TypeDecl>(Found))
    return H->internReflection(
        APValue(ReflectionKind::Type, C.getTypeDeclType(TyD).getAsOpaquePtr()));
  if (auto *ND = dyn_cast<NamespaceDecl>(Found))
    return H->internReflection(APValue(ReflectionKind::Namespace, ND));
  if (auto *VD = dyn_cast<ValueDecl>(Found))
    return H->internReflection(APValue(ReflectionKind::Declaration, VD));
  return 0;
}

static bool hostMetaEqual(uint64_t LHS, uint64_t RHS) {
  ActiveHostState *H = CurrentHost;
  if (!H)
    return LHS == RHS;
  if (LHS == RHS)
    return true;
  APValue L = H->lookupReflection(LHS);
  APValue R = H->lookupReflection(RHS);
  if (L.getReflectionKind() != R.getReflectionKind())
    return false;
  if (L.isReflectedType())
    return H->Ctx.hasSameType(L.getReflectedType(), R.getReflectedType());
  if (L.isReflectedDecl())
    return L.getReflectedDecl()->getCanonicalDecl() ==
           R.getReflectedDecl()->getCanonicalDecl();
  return false;
}

static void hostReportDiag(uint32_t Kind, const char *Tag, size_t TagLen,
                           const char *Msg, size_t MsgLen) {
  ActiveHostState *H = CurrentHost;
  if (!H)
    return;
  llvm::StringRef TagStr(Tag ? Tag : "", Tag ? TagLen : 0);
  llvm::StringRef MsgStr(Msg ? Msg : "", Msg ? MsgLen : 0);
  if (Kind == 0) {
    DiagnosticsEngine &Diags = H->Ctx.getDiagnostics();
    Diags.setLastDiagnosticIgnored(false);
    if (TagStr.empty())
      Diags.Report(H->CallLoc, diag::note_constexpr_message) << MsgStr;
    else
      Diags.Report(H->CallLoc, diag::note_constexpr_message_tag)
          << TagStr << MsgStr;
    return;
  }
  if (Kind == 1) {
    DiagnosticsEngine &Diags = H->Ctx.getDiagnostics();
    if (TagStr.empty())
      Diags.Report(H->CallLoc, diag::warn_constexpr_message) << MsgStr;
    else
      Diags.Report(H->CallLoc, diag::warn_constexpr_message_tag)
          << TagStr << MsgStr;
    return;
  }
  H->Failed = true;
  H->ErrorTag.assign(TagStr.data(), TagStr.size());
  H->ErrorMsg.assign(MsgStr.data(), MsgStr.size());
  if (H->HasErrorJump)
    std::longjmp(H->ErrorJump, 1);
}

static void hostQueueInjection(uint64_t TargetNSHandle,
                               uint64_t TokenSeqHandle) {
  ActiveHostState *H = CurrentHost;
  if (!H || !H->EvalStatus)
    return;
  if (!H->AllowInjection) {
    H->Failed = true;
    H->ErrorMsg = "queue_injection not allowed in this context";
    return;
  }
  DeclContext *TargetDC = nullptr;
  if (TargetNSHandle != 0) {
    APValue NS = H->lookupReflection(TargetNSHandle);
    if (NS.isReflectedNamespace())
      TargetDC = dyn_cast<DeclContext>(NS.getReflectedNamespace());
  }
  TokenSequenceData TSD = H->lookupTokenSeq(TokenSeqHandle);
  H->EvalStatus->PendingInjections.push_back({H->CallLoc, TargetDC, TSD});
}

static uint64_t hostMakeIdOrStrLit(bool IsStrLit, const ConstevalArg *Parts,
                                   size_t NumParts) {
  ActiveHostState *H = CurrentHost;
  if (!H)
    return 0;
  ASTContext &C = H->Ctx;
  SourceLocation Loc = H->CallLoc;

  std::string Buf;
  for (size_t I = 0; I < NumParts; ++I) {
    const ConstevalArg &P = Parts[I];
    if (P.Kind == CEK_CharPtr) {
      const char *S =
          reinterpret_cast<const char *>(static_cast<uintptr_t>(P.Val));
      if (S)
        Buf.append(S, P.Len ? P.Len : std::strlen(S));
    } else if (P.Kind == CEK_Int) {
      Buf += std::to_string(static_cast<int64_t>(P.Val));
    }
  }

  Token Tok;
  Tok.startToken();
  Tok.setLocation(Loc);
  if (IsStrLit) {
    std::string Quoted = "\"";
    for (char Ch : Buf) {
      if (Ch == '\n') {
        Quoted += "\\n";
        continue;
      }
      if (Ch == '\r') {
        Quoted += "\\r";
        continue;
      }
      if (Ch == '\t') {
        Quoted += "\\t";
        continue;
      }
      if (Ch == '"' || Ch == '\\')
        Quoted.push_back('\\');
      Quoted.push_back(Ch);
    }
    Quoted.push_back('"');
    const char *LitBuf = H->copyString(Quoted);
    Tok.setKind(tok::string_literal);
    Tok.setLiteralData(LitBuf);
    Tok.setLength(Quoted.size());
  } else {
    IdentifierInfo &II = C.Idents.get(Buf);
    Tok.setKind(tok::identifier);
    Tok.setIdentifierInfo(&II);
    Tok.setLength(II.getLength());
  }
  Token Toks[] = {Tok};
  return H->internTokenSeq(CreateTokenSequenceData(C, Toks));
}

static const char *hostStringizeTokens(uint64_t TokenSeqHandle) {
  ActiveHostState *H = CurrentHost;
  if (!H)
    return "";
  TokenSequenceData TSD = H->lookupTokenSeq(TokenSeqHandle);
  std::string Out;
  for (size_t I = 0; I < TSD.size(); ++I) {
    const Token &T = TSD[I];
    if (I > 0 && T.hasLeadingSpace())
      Out.push_back(' ');
    if (T.is(tok::identifier) && T.getIdentifierInfo())
      Out += T.getIdentifierInfo()->getName();
    else if (const char *Sp = tok::getKeywordSpelling(T.getKind()))
      Out += Sp;
    else if (const char *Sp = tok::getPunctuatorSpelling(T.getKind()))
      Out += Sp;
    else if (T.isLiteral() && T.getLiteralData())
      Out.append(T.getLiteralData(), T.getLength());
  }
  return H->copyString(Out);
}

static const ConstevalHostAPI GlobalHostAPI = {
    /*Version=*/1,
    /*Reserved=*/0,
    &hostCallMetafn,
    &hostMakeTokenSeq,
    &hostReflectBuiltin,
    &hostReflectNamed,
    &hostMetaEqual,
    &hostReportDiag,
    &hostQueueInjection,
    &hostMakeIdOrStrLit,
    &hostStringizeTokens,
};

struct LoadedPlugin {
  llvm::sys::DynamicLibrary Lib;
};

static std::vector<LoadedPlugin> &getLoadedPlugins(
    const std::vector<std::string> &Paths) {
  static llvm::StringMap<llvm::sys::DynamicLibrary> Cache;
  static std::vector<LoadedPlugin> Active;
  Active.clear();

  for (const std::string &P : Paths) {
    auto It = Cache.find(P);
    if (It == Cache.end()) {
      std::string Err;
      auto DL = llvm::sys::DynamicLibrary::getPermanentLibrary(P.c_str(), &Err);
      if (!DL.isValid())
        continue;
      using InitFn = void (*)(const ConstevalHostAPI *);
      if (void *Sym = DL.getAddressOfSymbol("__clang_consteval_init"))
        reinterpret_cast<InitFn>(Sym)(&GlobalHostAPI);
      It = Cache.try_emplace(P, DL).first;
    }
    Active.push_back({It->second});
  }
  return Active;
}

/// Marshal an APValue into a raw byte buffer matching the C++ layout of `Ty`.
static bool marshalArg(ActiveHostState &H, QualType Ty, const APValue &V,
                       uint8_t *Buf, size_t BufSize) {
  ASTContext &C = H.Ctx;
  Ty = Ty.getCanonicalType().getUnqualifiedType();

  if (Ty->isReflectionType()) {
    uint64_t Handle = H.internReflection(V);
    std::memcpy(Buf, &Handle, std::min(BufSize, sizeof(Handle)));
    return true;
  }
  if (Ty->isTokenSequenceType() || Ty->isMetaTokenType()) {
    uint64_t Handle = V.isTokenSequence() ? H.internTokenSeq(V.getTokenSequence()) : 0;
    std::memcpy(Buf, &Handle, std::min(BufSize, sizeof(Handle)));
    return true;
  }
  if (Ty->isBooleanType()) {
    bool B = V.isInt() && V.getInt().getBoolValue();
    std::memcpy(Buf, &B, 1);
    return true;
  }
  if (Ty->isIntegralOrEnumerationType()) {
    uint64_t Val = V.isInt() ? V.getInt().getZExtValue() : 0;
    std::memcpy(Buf, &Val, std::min(BufSize, sizeof(Val)));
    return true;
  }
  if (Ty->isPointerType() && Ty->getPointeeType()->isCharType()) {
    const char *Ptr = nullptr;
    if (V.isLValue()) {
      if (const auto *SL = dyn_cast_or_null<StringLiteral>(
              V.getLValueBase().dyn_cast<const Expr *>())) {
        Ptr = H.copyString(SL->getString());
      }
    }
    std::memcpy(Buf, &Ptr, std::min(BufSize, sizeof(Ptr)));
    return true;
  }
  if (Ty->isStdMetaTokenSequenceType()) {
    uint64_t Handle = 0;
    if (V.isTokenSequence())
      Handle = H.internTokenSeq(V.getTokenSequence());
    else if (V.isStruct() && V.getStructNumFields() == 1 &&
             V.getStructField(0).isTokenSequence())
      Handle = H.internTokenSeq(V.getStructField(0).getTokenSequence());
    std::memcpy(Buf, &Handle, std::min(BufSize, sizeof(Handle)));
    return true;
  }
  if (const auto *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    const ASTRecordLayout &Layout = C.getASTRecordLayout(RD);
    unsigned FieldIdx = 0;
    for (const FieldDecl *FD : RD->fields()) {
      uint64_t Offset = Layout.getFieldOffset(FieldIdx) / 8;
      size_t FieldSize = C.getTypeSizeInChars(FD->getType()).getQuantity();
      if (Offset + FieldSize <= BufSize && FieldIdx < V.getStructNumFields()) {
        if (!marshalArg(H, FD->getType(), V.getStructField(FieldIdx),
                        Buf + Offset, FieldSize))
          return false;
      }
      ++FieldIdx;
    }
    return true;
  }
  return false;
}

static bool unmarshalResult(ActiveHostState &H, QualType Ty,
                            const uint8_t *Buf, size_t BufSize,
                            APValue &Result) {
  ASTContext &C = H.Ctx;
  Ty = Ty.getCanonicalType().getUnqualifiedType();

  if (Ty->isVoidType()) {
    Result = APValue();
    return true;
  }
  if (Ty->isReflectionType()) {
    uint64_t Handle = 0;
    std::memcpy(&Handle, Buf, std::min(BufSize, sizeof(Handle)));
    Result = H.lookupReflection(Handle);
    return true;
  }
  if (Ty->isTokenSequenceType() || Ty->isMetaTokenType()) {
    uint64_t Handle = 0;
    std::memcpy(&Handle, Buf, std::min(BufSize, sizeof(Handle)));
    Result = APValue(H.lookupTokenSeq(Handle));
    return true;
  }
  if (Ty->isStdMetaTokenSequenceType()) {
    uint64_t Handle = 0;
    std::memcpy(&Handle, Buf, std::min(BufSize, sizeof(Handle)));
    Result = APValue(APValue::UninitStruct(), 0, 1);
    Result.getStructField(0) = APValue(H.lookupTokenSeq(Handle));
    return true;
  }
  if (Ty->isBooleanType()) {
    bool B = Buf[0] != 0;
    Result = APValue(C.MakeIntValue(B ? 1 : 0, C.BoolTy));
    return true;
  }
  if (Ty->isIntegralOrEnumerationType()) {
    uint64_t Val = 0;
    std::memcpy(&Val, Buf, std::min(BufSize, sizeof(Val)));
    Result = APValue(C.MakeIntValue(Val, Ty));
    return true;
  }
  if (Ty->isPointerType() && Ty->getPointeeType()->isCharType()) {
    const char *Ptr = nullptr;
    std::memcpy(&Ptr, Buf, std::min(BufSize, sizeof(Ptr)));
    if (!Ptr) {
      Result = APValue(APValue::LValueBase(), CharUnits::Zero(),
                       APValue::NoLValuePath(), /*CallIndex=*/0);
      return true;
    }
    llvm::StringRef SR(Ptr);
    auto *SL = StringLiteral::Create(
        C, SR, StringLiteralKind::Ordinary, /*Pascal=*/false,
        C.getStringLiteralArrayType(C.CharTy, SR.size()), H.CallLoc);
    APValue::LValuePathEntry Path[1] = {
        APValue::LValuePathEntry::ArrayIndex(0)};
    Result = APValue(SL, CharUnits::Zero(), Path, /*OnePastTheEnd=*/false);
    return true;
  }
  if (const auto *CAT = C.getAsConstantArrayType(Ty)) {
    uint64_t N = CAT->getZExtSize();
    QualType ElemTy = CAT->getElementType();
    size_t ElemSz = C.getTypeSizeInChars(ElemTy).getQuantity();
    Result = APValue(APValue::UninitArray(), N, N);
    for (uint64_t I = 0; I < N; ++I) {
      if (I * ElemSz + ElemSz > BufSize)
        return false;
      if (!unmarshalResult(H, ElemTy, Buf + I * ElemSz, ElemSz,
                           Result.getArrayInitializedElt(I)))
        return false;
    }
    return true;
  }
  if (const auto *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    const ASTRecordLayout &Layout = C.getASTRecordLayout(RD);
    unsigned NumFields = std::distance(RD->field_begin(), RD->field_end());
    Result = APValue(APValue::UninitStruct(), 0, NumFields);
    unsigned FieldIdx = 0;
    for (const FieldDecl *FD : RD->fields()) {
      uint64_t Offset = Layout.getFieldOffset(FieldIdx) / 8;
      size_t FieldSize = C.getTypeSizeInChars(FD->getType()).getQuantity();
      if (Offset + FieldSize <= BufSize) {
        if (!unmarshalResult(H, FD->getType(), Buf + Offset, FieldSize,
                             Result.getStructField(FieldIdx)))
          return false;
      }
      ++FieldIdx;
    }
    return true;
  }
  return false;
}

static void callThunkWithErrorJump(ActiveHostState &Host,
                                   ConstevalThunkFn Thunk,
                                   const void *const *ArgPtrs, void *RetBuf) {
  Host.HasErrorJump = true;
  if (setjmp(Host.ErrorJump) == 0)
    Thunk(ArgPtrs, RetBuf);
  Host.HasErrorJump = false;
}

static ConstevalThunkFn lookupPluginThunk(ASTContext &Ctx,
                                          const FunctionDecl *FD) {
  const auto &Paths = Ctx.getLangOpts().ConstevalPluginPaths;
  if (Paths.empty())
    return nullptr;

  std::unique_ptr<MangleContext> MC(Ctx.createMangleContext());
  SmallString<128> Mangled;
  llvm::raw_svector_ostream OS(Mangled);
  MC->mangleName(FD, OS);

  std::string ThunkName = ("__clang_ce_thunk_" + Mangled).str();
  for (auto &P : getLoadedPlugins(Paths)) {
    if (void *Sym = P.Lib.getAddressOfSymbol(ThunkName.c_str()))
      return reinterpret_cast<ConstevalThunkFn>(Sym);
  }
  return nullptr;
}

} // namespace

bool clang::hasConstevalPluginSymbol(ASTContext &Ctx, const FunctionDecl *FD) {
  return lookupPluginThunk(Ctx, FD) != nullptr;
}

int clang::invokeConstevalPlugin(ASTContext &Ctx, MetaActions &Meta,
                                 const FunctionDecl *FD, SourceLocation CallLoc,
                                 Decl *ContainingDecl, bool AllowInjection,
                                 Expr::EvalStatus *EvalStatus,
                                 llvm::ArrayRef<APValue> Args, APValue &Result,
                                 std::string &ErrorTag, std::string &ErrorMsg) {
  ConstevalThunkFn Thunk = lookupPluginThunk(Ctx, FD);
  if (!Thunk)
    return -1;

  llvm::TimeTraceScope TimeScope("ConstevalPlugin", [&] {
    return FD->getQualifiedNameAsString();
  });

  ActiveHostState Host{Ctx, Meta, CallLoc, ContainingDecl, AllowInjection,
                       EvalStatus, ErrorTag, ErrorMsg};
  ActiveHostState *PrevHost = CurrentHost;
  CurrentHost = &Host;

  // Marshal each argument into a scratch buffer.
  // For instance member functions, Args[0] is `*this`.
  SmallVector<const void *, 8> ArgPtrs;
  ArgPtrs.reserve(Args.size());
  const auto *MD = dyn_cast<CXXMethodDecl>(FD);
  unsigned ParamOffset = 0;
  if (MD && MD->isInstance() && !Args.empty()) {
    QualType ThisObjTy = Ctx.getCanonicalTagType(MD->getParent());
    size_t Sz =
        std::max<size_t>(8, Ctx.getTypeSizeInChars(ThisObjTy).getQuantity());
    uint8_t *Buf = Host.ScratchAlloc.Allocate<uint8_t>(Sz);
    std::memset(Buf, 0, Sz);
    if (!marshalArg(Host, ThisObjTy, Args[0], Buf, Sz)) {
      CurrentHost = PrevHost;
      ErrorMsg = "unsupported receiver type for consteval plugin";
      return 0;
    }
    ArgPtrs.push_back(Buf);
    ParamOffset = 1;
  }
  for (unsigned I = 0; I + ParamOffset < Args.size() && I < FD->getNumParams();
       ++I) {
    QualType ParamTy = FD->getParamDecl(I)->getType();
    if (ParamTy->isReferenceType())
      ParamTy = ParamTy.getNonReferenceType();
    size_t Sz = std::max<size_t>(8, Ctx.getTypeSizeInChars(ParamTy).getQuantity());
    uint8_t *Buf = Host.ScratchAlloc.Allocate<uint8_t>(Sz);
    std::memset(Buf, 0, Sz);
    if (!marshalArg(Host, ParamTy, Args[I + ParamOffset], Buf, Sz)) {
      CurrentHost = PrevHost;
      ErrorMsg = "unsupported parameter type for consteval plugin";
      return 0;
    }
    ArgPtrs.push_back(Buf);
  }

  QualType RetTy = FD->getReturnType();
  size_t RetSz =
      RetTy->isVoidType()
          ? 8
          : std::max<size_t>(8, Ctx.getTypeSizeInChars(RetTy).getQuantity());
  uint8_t *RetBuf = Host.ScratchAlloc.Allocate<uint8_t>(RetSz);
  std::memset(RetBuf, 0, RetSz);

  // Invoke the native plugin thunk!
  callThunkWithErrorJump(Host, Thunk, ArgPtrs.data(), RetBuf);

  CurrentHost = PrevHost;
  if (Host.Failed)
    return 0;

  if (!unmarshalResult(Host, RetTy, RetBuf, RetSz, Result)) {
    ErrorMsg = "unsupported return type for consteval plugin";
    return 0;
  }
  return 1;
}
