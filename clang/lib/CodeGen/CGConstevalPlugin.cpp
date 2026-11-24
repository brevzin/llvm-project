//===--- CGConstevalPlugin.cpp - CodeGen for native consteval plugins -----===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ConstevalPlugin.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Metafunction.h"
#include "clang/AST/Reflection.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Token.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {

// Field indices inside ConstevalHostAPI (must match ConstevalPlugin.h):
//   0: uint32_t Version
//   1: uint32_t Reserved
//   2: call_metafn
//   3: make_token_seq
//   4: reflect_builtin
//   5: reflect_named
//   6: meta_equal
//   7: report_diag
//   8: queue_injection
//   9: make_id_or_strlit
//  10: stringize_tokens
static llvm::StructType *getHostAPIType(llvm::LLVMContext &Ctx) {
  auto *I32 = llvm::Type::getInt32Ty(Ctx);
  auto *Ptr = llvm::PointerType::getUnqual(Ctx);
  return llvm::StructType::get(
      Ctx, {I32, I32, Ptr, Ptr, Ptr, Ptr, Ptr, Ptr, Ptr, Ptr, Ptr});
}

// ConstevalArg: { uint32_t Kind; uint32_t Aux; uint64_t Val; uint64_t Len; }
static llvm::StructType *getConstevalArgType(llvm::LLVMContext &Ctx) {
  auto *I32 = llvm::Type::getInt32Ty(Ctx);
  auto *I64 = llvm::Type::getInt64Ty(Ctx);
  return llvm::StructType::get(Ctx, {I32, I32, I64, I64});
}

// ConstevalTokenDesc: { uint16_t TokKind; uint16_t SpliceIdx; const char *Text; uint32_t TextLen; }
static llvm::StructType *getConstevalTokenDescType(llvm::LLVMContext &Ctx) {
  auto *I16 = llvm::Type::getInt16Ty(Ctx);
  auto *I32 = llvm::Type::getInt32Ty(Ctx);
  auto *Ptr = llvm::PointerType::getUnqual(Ctx);
  return llvm::StructType::get(Ctx, {I16, I16, Ptr, I32});
}

static llvm::Value *loadHostField(CodeGenFunction &CGF, unsigned FieldIdx) {
  llvm::LLVMContext &LCtx = CGF.getLLVMContext();
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  llvm::GlobalVariable *HostGV = CGF.CGM.getConstevalHostPtr();
  llvm::Value *HostPtr =
      CGF.Builder.CreateLoad(Address(HostGV, PtrTy, CGF.getPointerAlign()));
  auto *APITy = getHostAPIType(LCtx);
  llvm::Value *FieldPtr =
      CGF.Builder.CreateStructGEP(APITy, HostPtr, FieldIdx, "ce.api.slot");
  return CGF.Builder.CreateLoad(
      Address(FieldPtr, PtrTy, CGF.getPointerAlign()), "ce.api.fn");
}

static llvm::Value *emitTokenSeqHandle(CodeGenFunction &CGF, const Expr *E) {
  auto *I64 = llvm::Type::getInt64Ty(CGF.getLLVMContext());
  QualType QT = E->getType().getCanonicalType().getUnqualifiedType();
  if (QT->isStdMetaTokenSequenceType()) {
    Address Addr = Address::invalid();
    if (E->isGLValue()) {
      Addr = CGF.EmitLValue(E).getAddress();
    } else {
      Addr = CGF.CreateMemTemp(E->getType(), "ce.ts.tmp");
      CGF.EmitAggExpr(
          E, AggValueSlot::forAddr(Addr, E->getType().getQualifiers(),
                                   AggValueSlot::IsDestructed,
                                   AggValueSlot::DoesNotNeedGCBarriers,
                                   AggValueSlot::IsNotAliased,
                                   AggValueSlot::DoesNotOverlap));
    }
    return CGF.Builder.CreateLoad(Addr.withElementType(I64), "ce.ts.handle");
  }
  llvm::Value *V = CGF.EmitScalarExpr(E);
  return CGF.Builder.CreateZExtOrTrunc(V, I64);
}

static void storeConstevalArg(CodeGenFunction &CGF, llvm::Value *SlotPtr,
                              const Expr *ArgE) {
  llvm::LLVMContext &LCtx = CGF.getLLVMContext();
  auto *ArgTy = getConstevalArgType(LCtx);
  auto *I32 = llvm::Type::getInt32Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);

  QualType QT = ArgE->getType().getCanonicalType().getUnqualifiedType();
  uint32_t Kind = CEK_Int;
  llvm::Value *Val = llvm::ConstantInt::get(I64, 0);
  llvm::Value *Len = llvm::ConstantInt::get(I64, 0);

  if (QT->isReflectionType()) {
    Kind = CEK_MetaInfo;
    llvm::Value *V = CGF.EmitScalarExpr(ArgE);
    Val = CGF.Builder.CreateZExtOrTrunc(V, I64);
  } else if (QT->isTokenSequenceType() || QT->isMetaTokenType() ||
             QT->isStdMetaTokenSequenceType()) {
    Kind = QT->isMetaTokenType() ? CEK_MetaToken : CEK_TokenSequence;
    Val = emitTokenSeqHandle(CGF, ArgE);
  } else if (QT->isBooleanType()) {
    Kind = CEK_Bool;
    llvm::Value *V = CGF.EmitScalarExpr(ArgE);
    Val = CGF.Builder.CreateZExt(V, I64);
  } else if (QT->isIntegralOrEnumerationType()) {
    Kind = CEK_Int;
    llvm::Value *V = CGF.EmitScalarExpr(ArgE);
    Val = CGF.Builder.CreateZExtOrTrunc(V, I64);
  } else if (QT->isPointerType()) {
    QualType PT = QT->getPointeeType().getCanonicalType().getUnqualifiedType();
    if (PT->isCharType())
      Kind = CEK_CharPtr;
    else if (PT->isReflectionType())
      Kind = CEK_MetaInfoPtr;
    else if (PT->isMetaTokenType() || PT->isTokenSequenceType())
      Kind = CEK_TokenPtr;
    else
      Kind = CEK_VoidPtr;
    llvm::Value *V = CGF.EmitScalarExpr(ArgE);
    Val = CGF.Builder.CreatePtrToInt(V, I64);
  } else {
    CGF.EmitIgnoredExpr(ArgE);
    Kind = CEK_Void;
  }

  auto StoreField = [&](unsigned Idx, llvm::Value *V) {
    llvm::Value *GEP = CGF.Builder.CreateStructGEP(ArgTy, SlotPtr, Idx);
    CGF.Builder.CreateStore(V, Address(GEP, V->getType(), CharUnits::fromQuantity(4)));
  };
  StoreField(0, llvm::ConstantInt::get(I32, Kind));
  StoreField(1, llvm::ConstantInt::get(I32, 0));
  StoreField(2, Val);
  StoreField(3, Len);
}

} // namespace

llvm::GlobalVariable *CodeGenModule::getConstevalHostPtr() {
  if (auto *GV = getModule().getNamedGlobal("__clang_consteval_host"))
    return GV;
  auto *PtrTy = llvm::PointerType::getUnqual(getLLVMContext());
  return new llvm::GlobalVariable(
      getModule(), PtrTy, /*isConstant=*/false,
      llvm::GlobalValue::InternalLinkage,
      llvm::ConstantPointerNull::get(PtrTy), "__clang_consteval_host");
}

void CodeGenModule::EmitConstevalPluginInit() {
  if (!getLangOpts().ConstevalPlugin)
    return;
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *VoidTy = llvm::Type::getVoidTy(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  auto *FTy = llvm::FunctionType::get(VoidTy, {PtrTy}, /*isVarArg=*/false);
  llvm::Function *InitFn = llvm::Function::Create(
      FTy, llvm::GlobalValue::ExternalLinkage, "__clang_consteval_init",
      &getModule());
  InitFn->setVisibility(llvm::GlobalValue::DefaultVisibility);
  auto *BB = llvm::BasicBlock::Create(LCtx, "entry", InitFn);
  llvm::IRBuilder<> B(BB);
  B.CreateStore(InitFn->getArg(0), getConstevalHostPtr());
  B.CreateRetVoid();
}

void CodeGenModule::EmitConstevalPluginThunk(const FunctionDecl *FD) {
  if (!getLangOpts().ConstevalPlugin)
    return;
  if (!FD->isConsteval() || !FD->hasBody() || FD->isTemplated() ||
      isa<CXXConstructorDecl, CXXDestructorDecl>(FD) ||
      getContext().getSourceManager().isInSystemHeader(FD->getLocation()))
    return;

  StringRef Mangled = getMangledName(FD);
  std::string ThunkName = ("__clang_ce_thunk_" + Mangled).str();
  if (getModule().getNamedValue(ThunkName))
    return;

  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *VoidTy = llvm::Type::getVoidTy(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  auto *FTy = llvm::FunctionType::get(VoidTy, {PtrTy, PtrTy}, false);
  llvm::Function *ThunkFn = llvm::Function::Create(
      FTy, llvm::GlobalValue::ExternalLinkage, ThunkName, &getModule());
  ThunkFn->setVisibility(llvm::GlobalValue::DefaultVisibility);

  // Use CodeGenFunction to call FD with its true C++ ABI!
  CodeGenFunction CGF(*this);
  CGF.CurFn = ThunkFn;
  auto *EntryBB = llvm::BasicBlock::Create(LCtx, "entry", ThunkFn);
  CGF.Builder.SetInsertPoint(EntryBB);

  llvm::Value *ArgPtrsBase = ThunkFn->getArg(0);
  llvm::Value *RetPtr = ThunkFn->getArg(1);

  CallArgList CallArgs;
  unsigned ArgOffset = 0;
  const auto *MD = dyn_cast<CXXMethodDecl>(FD);
  if (MD && MD->isInstance()) {
    llvm::Value *ThisSlotPtr =
        CGF.Builder.CreateConstInBoundsGEP1_64(PtrTy, ArgPtrsBase, 0);
    llvm::Value *ThisAddr = CGF.Builder.CreateLoad(
        Address(ThisSlotPtr, PtrTy, getPointerAlign()));
    CallArgs.add(RValue::get(ThisAddr), MD->getThisType());
    ArgOffset = 1;
  }
  for (unsigned I = 0, N = FD->getNumParams(); I < N; ++I) {
    QualType ParamTy = FD->getParamDecl(I)->getType();
    llvm::Value *SlotPtrPtr = CGF.Builder.CreateConstInBoundsGEP1_64(
        PtrTy, ArgPtrsBase, I + ArgOffset);
    llvm::Value *ArgAddr = CGF.Builder.CreateLoad(
        Address(SlotPtrPtr, PtrTy, getPointerAlign()));

    if (ParamTy->isReferenceType()) {
      CallArgs.add(RValue::get(ArgAddr), ParamTy);
    } else if (CGF.hasAggregateEvaluationKind(ParamTy)) {
      llvm::Type *LLTy = getTypes().ConvertTypeForMem(ParamTy);
      Address A(ArgAddr, LLTy, getContext().getTypeAlignInChars(ParamTy));
      CallArgs.add(RValue::getAggregate(A), ParamTy);
    } else {
      llvm::Type *LLTy = getTypes().ConvertTypeForMem(ParamTy);
      Address A(ArgAddr, LLTy, getContext().getTypeAlignInChars(ParamTy));
      llvm::Value *Val = CGF.EmitLoadOfScalar(A, /*Volatile=*/false, ParamTy,
                                              FD->getLocation());
      CallArgs.add(RValue::get(Val), ParamTy);
    }
  }

  const CGFunctionInfo &FnInfo =
      (MD && MD->isInstance()) ? getTypes().arrangeCXXMethodDeclaration(MD)
                               : getTypes().arrangeGlobalDeclaration(FD);
  llvm::Constant *CalleeAddr = GetAddrOfFunction(FD);
  CGCallee Callee = CGCallee::forDirect(CalleeAddr, GlobalDecl(FD));

  QualType RetTy = FD->getReturnType();
  if (RetTy->isVoidType()) {
    CGF.EmitCall(FnInfo, Callee, ReturnValueSlot(), CallArgs);
  } else if (CGF.hasAggregateEvaluationKind(RetTy)) {
    llvm::Type *LLTy = getTypes().ConvertTypeForMem(RetTy);
    Address RetAddr(RetPtr, LLTy, getContext().getTypeAlignInChars(RetTy));
    CGF.EmitCall(FnInfo, Callee, ReturnValueSlot(RetAddr, false), CallArgs);
  } else {
    RValue RV = CGF.EmitCall(FnInfo, Callee, ReturnValueSlot(), CallArgs);
    llvm::Type *LLTy = getTypes().ConvertTypeForMem(RetTy);
    Address RetAddr(RetPtr, LLTy, getContext().getTypeAlignInChars(RetTy));
    CGF.EmitStoreOfScalar(RV.getScalarVal(), RetAddr, false, RetTy);
  }

  CGF.Builder.CreateRetVoid();
}

llvm::Value *CodeGenFunction::EmitConstevalPluginMetafnScalar(
    const CXXMetafunctionExpr *E) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I1 = llvm::Type::getInt1Ty(LCtx);
  auto *I32 = llvm::Type::getInt32Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  auto *ArgStructTy = getConstevalArgType(LCtx);

  size_t NumArgs = E->getNumArgs() > 0 ? E->getNumArgs() - 1 : 0;
  llvm::Value *ArgsArr = llvm::ConstantPointerNull::get(PtrTy);
  if (NumArgs > 0) {
    auto *ArrTy = llvm::ArrayType::get(ArgStructTy, NumArgs);
    Address Alloca = CreateTempAlloca(ArrTy, CharUnits::fromQuantity(8), "ce.args");
    ArgsArr = Alloca.emitRawPointer(*this);
    for (size_t I = 0; I < NumArgs; ++I) {
      llvm::Value *Slot = Builder.CreateConstInBoundsGEP1_64(
          ArgStructTy, ArgsArr, I);
      storeConstevalArg(*this, Slot, E->getArg(I + 1));
    }
  }

  Address OutAlloca =
      CreateTempAlloca(ArgStructTy, CharUnits::fromQuantity(8), "ce.ret");
  llvm::Value *OutPtr = OutAlloca.emitRawPointer(*this);

  llvm::Value *FnPtr = loadHostField(*this, /*call_metafn=*/2);
  auto *CallFTy = llvm::FunctionType::get(
      I1, {I32, I32, PtrTy, I64, PtrTy}, /*isVarArg=*/false);
  Builder.CreateCall(
      CallFTy, FnPtr,
      {llvm::ConstantInt::get(I32, E->getMetaFnID()),
       llvm::ConstantInt::get(I32, 0), ArgsArr,
       llvm::ConstantInt::get(I64, NumArgs), OutPtr});

  QualType ResTy = E->getType();
  if (ResTy->isVoidType())
    return nullptr;

  llvm::Value *ValGEP = Builder.CreateStructGEP(ArgStructTy, OutPtr, 2);
  llvm::Value *RawVal = Builder.CreateLoad(
      Address(ValGEP, I64, CharUnits::fromQuantity(8)), "ce.ret.val");

  llvm::Type *LLResTy = ConvertType(ResTy);
  if (LLResTy->isPointerTy())
    return Builder.CreateIntToPtr(RawVal, LLResTy);
  if (LLResTy->isIntegerTy())
    return Builder.CreateZExtOrTrunc(RawVal, LLResTy);
  return llvm::Constant::getNullValue(LLResTy);
}

llvm::Value *CodeGenFunction::EmitConstevalPluginTokenSeq(
    const CXXTokenSequenceExpr *E) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I16 = llvm::Type::getInt16Ty(LCtx);
  auto *I32 = llvm::Type::getInt32Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  auto *DescTy = getConstevalTokenDescType(LCtx);
  auto *ArgStructTy = getConstevalArgType(LCtx);

  TokenSequenceData TSD = E->getTokenSequence();
  SmallVector<llvm::Constant *, 32> DescInits;
  SmallVector<const Expr *, 8> SpliceExprs;

  for (const Token &Tok : TSD) {
    uint16_t Kind = static_cast<uint16_t>(Tok.getKind());
    uint16_t SpliceIdx = 0;
    llvm::Constant *TextPtr = llvm::ConstantPointerNull::get(PtrTy);
    uint32_t TextLen = 0;

    if (Tok.is(tok::annot_token_seq_expr)) {
      SpliceIdx = static_cast<uint16_t>(SpliceExprs.size());
      SpliceExprs.push_back(static_cast<const Expr *>(Tok.getAnnotationValue()));
    } else if (Tok.is(tok::identifier) && Tok.getIdentifierInfo()) {
      StringRef Name = Tok.getIdentifierInfo()->getName();
      TextPtr = CGM.GetAddrOfConstantCString(Name.str()).getPointer();
      TextLen = Name.size();
    } else if (Tok.isLiteral() && Tok.getLiteralData()) {
      StringRef Lit(Tok.getLiteralData(), Tok.getLength());
      TextPtr = CGM.GetAddrOfConstantCString(Lit.str()).getPointer();
      TextLen = Lit.size();
    }

    DescInits.push_back(llvm::ConstantStruct::get(
        DescTy,
        {llvm::ConstantInt::get(I16, Kind),
         llvm::ConstantInt::get(I16, SpliceIdx), TextPtr,
         llvm::ConstantInt::get(I32, TextLen)}));
  }

  llvm::Value *ToksPtr = llvm::ConstantPointerNull::get(PtrTy);
  if (!DescInits.empty()) {
    auto *ArrTy = llvm::ArrayType::get(DescTy, DescInits.size());
    auto *GV = new llvm::GlobalVariable(
        CGM.getModule(), ArrTy, /*isConstant=*/true,
        llvm::GlobalValue::PrivateLinkage,
        llvm::ConstantArray::get(ArrTy, DescInits), ".ce.toks");
    ToksPtr = GV;
  }

  llvm::Value *SplicesPtr = llvm::ConstantPointerNull::get(PtrTy);
  if (!SpliceExprs.empty()) {
    auto *ArrTy = llvm::ArrayType::get(ArgStructTy, SpliceExprs.size());
    Address Alloca =
        CreateTempAlloca(ArrTy, CharUnits::fromQuantity(8), "ce.splices");
    SplicesPtr = Alloca.emitRawPointer(*this);
    for (size_t I = 0; I < SpliceExprs.size(); ++I) {
      llvm::Value *Slot =
          Builder.CreateConstInBoundsGEP1_64(ArgStructTy, SplicesPtr, I);
      storeConstevalArg(*this, Slot, SpliceExprs[I]);
    }
  }

  llvm::Value *FnPtr = loadHostField(*this, /*make_token_seq=*/3);
  auto *CallFTy =
      llvm::FunctionType::get(I64, {PtrTy, I64, PtrTy, I64}, false);
  llvm::Value *H = Builder.CreateCall(
      CallFTy, FnPtr,
      {ToksPtr, llvm::ConstantInt::get(I64, DescInits.size()), SplicesPtr,
       llvm::ConstantInt::get(I64, SpliceExprs.size())});
  llvm::Type *LLResTy = ConvertType(E->getType());
  if (LLResTy->isIntegerTy())
    return Builder.CreateZExtOrTrunc(H, LLResTy);
  return H;
}

llvm::Value *CodeGenFunction::EmitConstevalPluginReflect(
    const CXXReflectExpr *E) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I32 = llvm::Type::getInt32Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);

  llvm::Value *H = nullptr;
  std::string QualName;
  if (!E->hasDependentSubExpr()) {
    APValue RV = E->getReflection();
    if (RV.isReflectedType()) {
      QualType QT = RV.getReflectedType().getCanonicalType();
      unsigned Encoded = 0;
      QualType BaseQT = QT;
      if (BaseQT->isPointerType()) {
        Encoded |= (1u << 16);
        BaseQT = BaseQT->getPointeeType();
      }
      if (BaseQT.isConstQualified()) {
        Encoded |= (1u << 15);
        BaseQT = BaseQT.getUnqualifiedType();
      }
      if (const auto *BT = dyn_cast<BuiltinType>(BaseQT)) {
        Encoded |= static_cast<unsigned>(BT->getKind());
        llvm::Value *FnPtr = loadHostField(*this, /*reflect_builtin=*/4);
        auto *FTy = llvm::FunctionType::get(I64, {I32}, false);
        H = Builder.CreateCall(FTy, FnPtr,
                               {llvm::ConstantInt::get(I32, Encoded)});
      } else if (const auto *TD = QT->getAsTagDecl()) {
        QualName = TD->getQualifiedNameAsString();
      }
    } else if (RV.isReflectedTemplate()) {
      if (const auto *TD = RV.getReflectedTemplate().getAsTemplateDecl())
        QualName = TD->getQualifiedNameAsString();
    } else if (RV.isReflectedNamespace()) {
      if (const auto *ND = dyn_cast<NamespaceDecl>(RV.getReflectedNamespace()))
        QualName = ND->getQualifiedNameAsString();
    }
  }
  if (!H) {
    llvm::Value *FnPtr = loadHostField(*this, /*reflect_named=*/5);
    auto *FTy = llvm::FunctionType::get(I64, {PtrTy, I64}, false);
    llvm::Value *NamePtr =
        QualName.empty()
            ? llvm::ConstantPointerNull::get(PtrTy)
            : CGM.GetAddrOfConstantCString(QualName).getPointer();
    H = Builder.CreateCall(FTy, FnPtr,
                           {NamePtr, llvm::ConstantInt::get(I64, QualName.size())});
  }
  return Builder.CreateZExtOrTrunc(H, ConvertType(E->getType()));
}

llvm::Value *CodeGenFunction::EmitConstevalPluginMetaEqual(llvm::Value *LHS,
                                                           llvm::Value *RHS) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I1 = llvm::Type::getInt1Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  llvm::Value *FnPtr = loadHostField(*this, /*meta_equal=*/6);
  auto *FTy = llvm::FunctionType::get(I1, {I64, I64}, false);
  return Builder.CreateCall(FTy, FnPtr,
                            {Builder.CreateZExtOrTrunc(LHS, I64),
                             Builder.CreateZExtOrTrunc(RHS, I64)});
}

void CodeGenFunction::EmitConstevalPluginQueueInjection(
    const CXXBuiltinInjectExpr *E) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *VoidTy = llvm::Type::getVoidTy(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);

  llvm::Value *NSVal = llvm::ConstantInt::get(I64, 0);
  if (E->hasTargetNS()) {
    llvm::Value *V = EmitScalarExpr(E->getTargetNS());
    NSVal = Builder.CreateZExtOrTrunc(V, I64);
  }
  llvm::Value *TSVal = emitTokenSeqHandle(*this, E->getOperand());
  llvm::Value *FnPtr = loadHostField(*this, /*queue_injection=*/8);
  auto *FTy = llvm::FunctionType::get(VoidTy, {I64, I64}, false);
  Builder.CreateCall(FTy, FnPtr, {NSVal, TSVal});
}

llvm::Value *CodeGenFunction::EmitConstevalPluginIdOrStrLit(
    bool IsStrLit, unsigned NumArgs, const Expr *const *Args,
    const Expr *const *SizeCalls, const Expr *const *DataCalls) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I1 = llvm::Type::getInt1Ty(LCtx);
  auto *I32 = llvm::Type::getInt32Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  auto *ArgStructTy = getConstevalArgType(LCtx);

  llvm::Value *PartsPtr = llvm::ConstantPointerNull::get(PtrTy);
  if (NumArgs > 0) {
    auto *ArrTy = llvm::ArrayType::get(ArgStructTy, NumArgs);
    Address Alloca =
        CreateTempAlloca(ArrTy, CharUnits::fromQuantity(8), "ce.id.parts");
    PartsPtr = Alloca.emitRawPointer(*this);
    for (unsigned I = 0; I < NumArgs; ++I) {
      llvm::Value *Slot =
          Builder.CreateConstInBoundsGEP1_64(ArgStructTy, PartsPtr, I);
      uint32_t Kind = CEK_CharPtr;
      llvm::Value *Val = llvm::ConstantInt::get(I64, 0);
      llvm::Value *Len = llvm::ConstantInt::get(I64, 0);

      if (SizeCalls[I] && DataCalls[I]) {
        // String-like object (string_view, string): call data() and size().
        llvm::Value *DP = EmitScalarExpr(DataCalls[I]);
        llvm::Value *SZ = EmitScalarExpr(SizeCalls[I]);
        Val = Builder.CreatePtrToInt(DP, I64);
        Len = Builder.CreateZExtOrTrunc(SZ, I64);
      } else if (Args[I]->getType()->isIntegralOrEnumerationType()) {
        Kind = CEK_Int;
        llvm::Value *V = EmitScalarExpr(Args[I]);
        Val = Args[I]->getType()->isSignedIntegerType()
                  ? Builder.CreateSExtOrTrunc(V, I64)
                  : Builder.CreateZExtOrTrunc(V, I64);
      } else if (Args[I]->getType()->isArrayType()) {
        // String literal or char[N]: emit lvalue address.
        llvm::Value *DP = EmitLValue(Args[I]).getPointer(*this);
        Val = Builder.CreatePtrToInt(DP, I64);
      } else {
        // const char*
        llvm::Value *DP = EmitScalarExpr(Args[I]);
        Val = Builder.CreatePtrToInt(DP, I64);
      }

      auto StoreF = [&](unsigned Idx, llvm::Value *V) {
        llvm::Value *GEP = Builder.CreateStructGEP(ArgStructTy, Slot, Idx);
        Builder.CreateStore(
            V, Address(GEP, V->getType(), CharUnits::fromQuantity(4)));
      };
      StoreF(0, llvm::ConstantInt::get(I32, Kind));
      StoreF(1, llvm::ConstantInt::get(I32, 0));
      StoreF(2, Val);
      StoreF(3, Len);
    }
  }

  llvm::Value *FnPtr = loadHostField(*this, /*make_id_or_strlit=*/9);
  auto *FTy = llvm::FunctionType::get(I64, {I1, PtrTy, I64}, false);
  return Builder.CreateCall(
      FTy, FnPtr,
      {llvm::ConstantInt::get(I1, IsStrLit ? 1 : 0), PartsPtr,
       llvm::ConstantInt::get(I64, NumArgs)});
}

llvm::Value *CodeGenFunction::EmitConstevalPluginStringize(
    const CXXBuiltinStringizeExpr *E) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  llvm::Value *TSVal = emitTokenSeqHandle(*this, E->getOperand());
  llvm::Value *FnPtr = loadHostField(*this, /*stringize_tokens=*/10);
  auto *FTy = llvm::FunctionType::get(PtrTy, {I64}, false);
  return Builder.CreateCall(FTy, FnPtr, {TSVal});
}

void CodeGenFunction::EmitConstevalPluginDiag(const CallExpr *E) {
  if (E->getNumArgs() < 5)
    return;
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *VoidTy = llvm::Type::getVoidTy(LCtx);
  auto *I32 = llvm::Type::getInt32Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);

  llvm::Value *Kind = Builder.CreateZExtOrTrunc(EmitScalarExpr(E->getArg(0)), I32);
  auto EmitPtr = [&](const Expr *Arg) -> llvm::Value * {
    if (Arg->getType()->isArrayType())
      return EmitLValue(Arg).getPointer(*this);
    return EmitScalarExpr(Arg);
  };
  llvm::Value *TagPtr = EmitPtr(E->getArg(1));
  llvm::Value *TagLen = Builder.CreateZExtOrTrunc(EmitScalarExpr(E->getArg(2)), I64);
  llvm::Value *MsgPtr = EmitPtr(E->getArg(3));
  llvm::Value *MsgLen = Builder.CreateZExtOrTrunc(EmitScalarExpr(E->getArg(4)), I64);

  llvm::Value *FnPtr = loadHostField(*this, /*report_diag=*/7);
  auto *FTy = llvm::FunctionType::get(VoidTy, {I32, PtrTy, I64, PtrTy, I64}, false);
  Builder.CreateCall(FTy, FnPtr, {Kind, TagPtr, TagLen, MsgPtr, MsgLen});
}
