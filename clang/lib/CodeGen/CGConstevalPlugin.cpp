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
#include "ConstantEmitter.h"
#include "clang/AST/ConstevalPlugin.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Metafunction.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Reflection.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Token.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {

static llvm::StructType *getHostAPIType(llvm::LLVMContext &Ctx) {
  auto *I32 = llvm::Type::getInt32Ty(Ctx);
  auto *Ptr = llvm::PointerType::getUnqual(Ctx);
  SmallVector<llvm::Type *, CEHF_NumFields> Fields(CEHF_NumFields, Ptr);
  Fields[CEHF_Version] = I32;
  Fields[CEHF_Reserved] = I32;
  return llvm::StructType::get(Ctx, Fields);
}

// ConstevalArg: { uint32_t Kind; uint32_t Aux; uint64_t Val; uint64_t Len; }
static llvm::StructType *getConstevalArgType(llvm::LLVMContext &Ctx) {
  auto *I32 = llvm::Type::getInt32Ty(Ctx);
  auto *I64 = llvm::Type::getInt64Ty(Ctx);
  return llvm::StructType::get(Ctx, {I32, I32, I64, I64});
}

// ConstevalTokenDesc: { uint16_t TokKind; uint16_t SpliceIdx; const char *Text;
//                       uint32_t TextLen; uint32_t Flags; }
static llvm::StructType *getConstevalTokenDescType(llvm::LLVMContext &Ctx) {
  auto *I16 = llvm::Type::getInt16Ty(Ctx);
  auto *I32 = llvm::Type::getInt32Ty(Ctx);
  auto *Ptr = llvm::PointerType::getUnqual(Ctx);
  return llvm::StructType::get(Ctx, {I16, I16, Ptr, I32, I32});
}

static llvm::Value *loadHostField(CodeGenFunction &CGF,
                                  ConstevalHostField Field) {
  llvm::LLVMContext &LCtx = CGF.getLLVMContext();
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  llvm::GlobalVariable *HostGV = CGF.CGM.getConstevalHostPtr();
  llvm::Value *HostPtr =
      CGF.Builder.CreateLoad(Address(HostGV, PtrTy, CGF.getPointerAlign()));
  auto *APITy = getHostAPIType(LCtx);
  llvm::Value *FieldPtr =
      CGF.Builder.CreateStructGEP(APITy, HostPtr, Field, "ce.api.slot");
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

/// An integer of at most 64 bits, extended to 64 as its type says.
static llvm::Value *emitInt64(CodeGenFunction &CGF, const Expr *E) {
  auto *I64 = llvm::Type::getInt64Ty(CGF.getLLVMContext());
  llvm::Value *V = CGF.EmitScalarExpr(E);
  return E->getType()->isSignedIntegerOrEnumerationType()
             ? CGF.Builder.CreateSExtOrTrunc(V, I64)
             : CGF.Builder.CreateZExtOrTrunc(V, I64);
}

static void storeConstevalArgFields(CodeGenFunction &CGF, llvm::Value *SlotPtr,
                                    uint32_t Kind, uint32_t Aux,
                                    llvm::Value *Val, llvm::Value *Len) {
  llvm::LLVMContext &LCtx = CGF.getLLVMContext();
  auto *ArgTy = getConstevalArgType(LCtx);
  auto *I32 = llvm::Type::getInt32Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto StoreField = [&](unsigned Idx, llvm::Value *V) {
    llvm::Value *GEP = CGF.Builder.CreateStructGEP(ArgTy, SlotPtr, Idx);
    CGF.Builder.CreateStore(
        V, Address(GEP, V->getType(), CharUnits::fromQuantity(4)));
  };
  StoreField(0, llvm::ConstantInt::get(I32, Kind));
  StoreField(1, llvm::ConstantInt::get(I32, Aux));
  StoreField(2, Val ? Val : llvm::ConstantInt::get(I64, 0));
  StoreField(3, Len ? Len : llvm::ConstantInt::get(I64, 0));
}

/// Store the value of \p ArgE -- an argument of a metafunction, or (if
/// \p ForSplice) an expression interpolated into a token sequence -- where
/// the host reads it.
static void storeConstevalArg(CodeGenFunction &CGF, llvm::Value *SlotPtr,
                              const Expr *ArgE, bool ForSplice) {
  llvm::LLVMContext &LCtx = CGF.getLLVMContext();
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  ASTContext &Ctx = CGF.getContext();

  QualType QT = ArgE->getType().getCanonicalType().getUnqualifiedType();
  uint32_t Kind = CEK_Void;
  uint32_t Aux = 0;
  llvm::Value *Val = nullptr;
  llvm::Value *Len = nullptr;

  auto Unsupported = [&] {
    CGF.EmitConstevalPluginUnsupported(
        ArgE->getExprLoc(),
        Twine("a value of type '") + ArgE->getType().getAsString() +
            (ForSplice ? "' cannot be interpolated into a token sequence in a "
                         "consteval plugin yet"
                       : "' cannot be passed to a metafunction in a consteval "
                         "plugin yet"));
    CGF.EmitIgnoredExpr(ArgE);
  };

  if (QT->isReflectionType()) {
    Kind = CEK_MetaInfo;
    llvm::Value *V = CGF.EmitScalarExpr(ArgE);
    Val = CGF.Builder.CreateZExtOrTrunc(V, I64);
  } else if (QT->isTokenSequenceType() || QT->isMetaTokenType() ||
             QT->isStdMetaTokenSequenceType()) {
    Kind = QT->isMetaTokenType() ? CEK_MetaToken : CEK_TokenSequence;
    Val = emitTokenSeqHandle(CGF, ArgE);
  } else if (QT->isIntegralOrEnumerationType() && !QT->isIncompleteType() &&
             Ctx.getTypeSize(QT) <= 64) {
    Kind = QT->isBooleanType() ? CEK_Bool : CEK_Int;
    Val = emitInt64(CGF, ArgE);
    if (ForSplice) {
      // The host makes the tokens for the value from its type: say which.
      MetaTokenEnumKind EnumKind = getMetaTokenEnumKind(QT);
      Aux = static_cast<uint32_t>(EnumKind);
      if (EnumKind == MetaTokenEnumKind::None) {
        std::string Desc, Error;
        if (!encodeConstevalPluginReflection(
                Ctx, APValue(ReflectionKind::Type, QT.getAsOpaquePtr()), Desc,
                Error))
          CGF.EmitConstevalPluginUnsupported(
              ArgE->getExprLoc(),
              "this value cannot be interpolated into a token sequence in a "
              "consteval plugin: " +
                  Error);
        Len = CGF.Builder.CreatePtrToInt(
            CGF.CGM.GetAddrOfConstantCString(Desc).getPointer(), I64);
      }
    }
  } else if ((QT->isPointerType() || QT->isArrayType()) && !ForSplice) {
    // (An array -- a string literal, say -- is its first element's address.)
    QualType PT =
        QT->getPointeeOrArrayElementType()->getCanonicalTypeUnqualified();
    if (PT->isCharType() || PT->isChar8Type())
      Kind = CEK_CharPtr;
    else if (PT->isReflectionType())
      Kind = CEK_MetaInfoPtr;
    else if (PT->isMetaTokenType() || PT->isTokenSequenceType())
      Kind = CEK_TokenPtr;
    else
      Kind = CEK_VoidPtr;
    llvm::Value *V = QT->isArrayType() ? CGF.EmitLValue(ArgE).getPointer(CGF)
                                       : CGF.EmitScalarExpr(ArgE);
    Val = CGF.Builder.CreatePtrToInt(V, I64);
  } else {
    Unsupported();
  }

  storeConstevalArgFields(CGF, SlotPtr, Kind, Aux, Val, Len);
}

} // namespace

// What a plugin defines besides its functions is the same in each of its
// translation units, and one definition of each is kept when they are linked.

llvm::GlobalVariable *CodeGenModule::getConstevalHostPtr() {
  if (auto *GV = getModule().getNamedGlobal("__clang_consteval_host"))
    return GV;
  auto *PtrTy = llvm::PointerType::getUnqual(getLLVMContext());
  auto *GV = new llvm::GlobalVariable(getModule(), PtrTy, /*isConstant=*/false,
                                      llvm::GlobalValue::WeakODRLinkage,
                                      llvm::ConstantPointerNull::get(PtrTy),
                                      "__clang_consteval_host");
  GV->setVisibility(llvm::GlobalValue::HiddenVisibility);
  return GV;
}

void CodeGenModule::EmitConstevalPluginInit() {
  if (!getLangOpts().ConstevalPlugin)
    return;
  // (A reflection is the host's handle for it, which needs the room.)
  if (getContext().getTypeSize(getContext().MetaInfoTy) < 64)
    Error(SourceLocation(),
          "-fconsteval-plugin cannot be used with -fconsteval-operations");
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *VoidTy = llvm::Type::getVoidTy(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  auto *FTy = llvm::FunctionType::get(VoidTy, {PtrTy}, /*isVarArg=*/false);
  llvm::Function *InitFn =
      llvm::Function::Create(FTy, llvm::GlobalValue::WeakODRLinkage,
                             ConstevalPluginInitSymbol, &getModule());
  InitFn->setVisibility(llvm::GlobalValue::DefaultVisibility);
  auto *BB = llvm::BasicBlock::Create(LCtx, "entry", InitFn);
  llvm::IRBuilder<> B(BB);
  B.CreateStore(InitFn->getArg(0), getConstevalHostPtr());
  B.CreateRetVoid();

  // Which compiler built the plugin: the one loading it has to be the same.
  llvm::Constant *ABI =
      llvm::ConstantDataArray::getString(LCtx, getConstevalPluginABI());
  auto *ABIVar = new llvm::GlobalVariable(
      getModule(), ABI->getType(), /*isConstant=*/true,
      llvm::GlobalValue::WeakODRLinkage, ABI, ConstevalPluginABISymbol);
  ABIVar->setVisibility(llvm::GlobalValue::DefaultVisibility);
}

void CodeGenModule::EmitConstevalPluginThunk(const FunctionDecl *FD) {
  if (!getLangOpts().ConstevalPlugin)
    return;
  if (!FD->isConsteval() || !FD->hasBody() || FD->isTemplated() ||
      isa<CXXConstructorDecl, CXXDestructorDecl>(FD) ||
      getContext().getSourceManager().isInSystemHeader(FD->getLocation()))
    return;

  StringRef Mangled = getMangledName(FD);
  std::string ThunkName = (ConstevalPluginThunkPrefix + Mangled).str();
  if (getModule().getNamedValue(ThunkName))
    return;

  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *VoidTy = llvm::Type::getVoidTy(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  auto *FTy = llvm::FunctionType::get(VoidTy, {PtrTy, PtrTy}, false);

  // How the plugin expects the arguments and the result to be laid out, for
  // the compiler loading it to check against its own declaration.
  llvm::Constant *Sig = llvm::ConstantDataArray::getString(
      LCtx, getConstevalPluginSignature(getContext(), FD));
  auto *SigVar = new llvm::GlobalVariable(
      getModule(), Sig->getType(), /*isConstant=*/true,
      llvm::GlobalValue::WeakODRLinkage, Sig,
      (ConstevalPluginSignaturePrefix + Mangled).str());
  SigVar->setVisibility(llvm::GlobalValue::DefaultVisibility);

  // The function that takes the arguments from where the host put them and
  // calls FD with its own calling convention. If exceptions are enabled it
  // is called by the thunk proper (below), which catches what it throws.
  const bool CatchExceptions = getLangOpts().CXXExceptions;
  llvm::Function *CallFn = llvm::Function::Create(
      FTy,
      CatchExceptions ? llvm::GlobalValue::InternalLinkage
                      : llvm::GlobalValue::WeakODRLinkage,
      CatchExceptions ? ("__clang_ce_call_" + Mangled).str() : ThunkName,
      &getModule());

  CodeGenFunction CGF(*this);
  CGF.CurFn = CallFn;
  auto *EntryBB = llvm::BasicBlock::Create(LCtx, "entry", CallFn);
  CGF.AllocaInsertPt = new llvm::BitCastInst(llvm::PoisonValue::get(Int32Ty),
                                             Int32Ty, "allocapt", EntryBB);
  CGF.Builder.SetInsertPoint(EntryBB);

  llvm::Value *ArgPtrsBase = CallFn->getArg(0);
  llvm::Value *RetPtr = CallFn->getArg(1);

  CallArgList CallArgs;
  unsigned ArgOffset = 0;
  const auto *MD = dyn_cast<CXXMethodDecl>(FD);
  const bool HasThis = MD && MD->isImplicitObjectMemberFunction();
  if (HasThis) {
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
      HasThis ? getTypes().arrangeCXXMethodDeclaration(MD)
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
  {
    llvm::Instruction *AllocaPt = CGF.AllocaInsertPt;
    CGF.AllocaInsertPt = nullptr;
    AllocaPt->eraseFromParent();
  }
  if (!CatchExceptions)
    return;

  // The thunk: an exception that leaves the function cannot go on into the
  // compiler. Catch it, whatever it is, and tell the host, which does not
  // return here.
  llvm::Function *ThunkFn = llvm::Function::Create(
      FTy, llvm::GlobalValue::WeakODRLinkage, ThunkName, &getModule());
  ThunkFn->setVisibility(llvm::GlobalValue::DefaultVisibility);
  ThunkFn->setPersonalityFn(cast<llvm::Constant>(
      CreateRuntimeFunction(llvm::FunctionType::get(Int32Ty, true),
                            "__gxx_personality_v0")
          .getCallee()));
  auto *ThunkEntry = llvm::BasicBlock::Create(LCtx, "entry", ThunkFn);
  auto *Done = llvm::BasicBlock::Create(LCtx, "done", ThunkFn);
  auto *LPad = llvm::BasicBlock::Create(LCtx, "lpad", ThunkFn);
  llvm::IRBuilder<> B(ThunkEntry);
  B.CreateInvoke(FTy, CallFn, Done, LPad,
                 {ThunkFn->getArg(0), ThunkFn->getArg(1)});
  B.SetInsertPoint(Done);
  B.CreateRetVoid();

  B.SetInsertPoint(LPad);
  llvm::LandingPadInst *LP =
      B.CreateLandingPad(llvm::StructType::get(PtrTy, Int32Ty), 1);
  LP->addClause(llvm::ConstantPointerNull::get(PtrTy));
  llvm::Value *Exn = B.CreateExtractValue(LP, 0);
  B.CreateCall(
      CreateRuntimeFunction(llvm::FunctionType::get(PtrTy, {PtrTy}, false),
                            "__cxa_begin_catch"),
      {Exn});
  B.CreateCall(CreateRuntimeFunction(llvm::FunctionType::get(VoidTy, false),
                                     "__cxa_end_catch"));
  auto *APITy = getHostAPIType(LCtx);
  llvm::Value *Host = B.CreateLoad(PtrTy, getConstevalHostPtr(), "ce.host");
  llvm::Value *Slot = B.CreateStructGEP(APITy, Host, CEHF_UnhandledException);
  llvm::Value *Fn = B.CreateLoad(PtrTy, Slot, "ce.api.fn");
  B.CreateCall(llvm::FunctionType::get(VoidTy, false), Fn);
  B.CreateRetVoid();
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
      storeConstevalArg(*this, Slot, E->getArg(I + 1), /*ForSplice=*/false);
    }
  }

  Address OutAlloca =
      CreateTempAlloca(ArgStructTy, CharUnits::fromQuantity(8), "ce.ret");
  llvm::Value *OutPtr = OutAlloca.emitRawPointer(*this);

  llvm::Value *FnPtr = loadHostField(*this, CEHF_CallMetafn);
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
  EmitConstevalPluginUnsupported(
      E->getExprLoc(), "a metafunction returning '" + ResTy.getAsString() +
                           "' cannot be called in a consteval plugin yet");
  return llvm::Constant::getNullValue(LLResTy);
}

llvm::Value *CodeGenFunction::EmitConstevalPluginTokenSeq(
    const CXXTokenSequenceExpr *E) {
  llvm::Value *H = EmitConstevalPluginTokens(E->getTokenSequence());
  llvm::Type *LLResTy = ConvertType(E->getType());
  if (LLResTy->isIntegerTy())
    return Builder.CreateZExtOrTrunc(H, LLResTy);
  return H;
}

llvm::Value *CodeGenFunction::EmitConstevalPluginTokens(TokenSequenceData TSD) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I16 = llvm::Type::getInt16Ty(LCtx);
  auto *I32 = llvm::Type::getInt32Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  auto *DescTy = getConstevalTokenDescType(LCtx);
  auto *ArgStructTy = getConstevalArgType(LCtx);

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
    } else if (Tok.isAnnotation() || Tok.is(tok::unknown)) {
      // (A '\' of a nested token sequence's interpolation is the latter.)
      EmitConstevalPluginUnsupported(
          Tok.getLocation(), "this token cannot be part of a token sequence "
                             "in a consteval plugin yet");
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
        DescTy, {llvm::ConstantInt::get(I16, Kind),
                 llvm::ConstantInt::get(I16, SpliceIdx), TextPtr,
                 llvm::ConstantInt::get(I32, TextLen),
                 llvm::ConstantInt::get(I32, Tok.getFlags())}));
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
      storeConstevalArg(*this, Slot, SpliceExprs[I], /*ForSplice=*/true);
    }
  }

  llvm::Value *FnPtr = loadHostField(*this, CEHF_MakeTokenSeq);
  auto *CallFTy =
      llvm::FunctionType::get(I64, {PtrTy, I64, PtrTy, I64}, false);
  return Builder.CreateCall(
      CallFTy, FnPtr,
      {ToksPtr, llvm::ConstantInt::get(I64, DescInits.size()), SplicesPtr,
       llvm::ConstantInt::get(I64, SpliceExprs.size())});
}

llvm::Value *CodeGenFunction::EmitConstevalPluginReflect(
    const CXXReflectExpr *E) {
  if (E->hasDependentSubExpr()) {
    EmitConstevalPluginUnsupported(
        E->getExprLoc(), "this reflection cannot be used in a consteval plugin "
                         "yet");
    return llvm::Constant::getNullValue(ConvertType(E->getType()));
  }
  return EmitConstevalPluginReflection(E->getReflection(), E->getExprLoc(),
                                       E->getType());
}

llvm::Value *CodeGenFunction::EmitConstevalPluginReflection(const APValue &RV,
                                                            SourceLocation Loc,
                                                            QualType Ty) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);

  // The host finds the reflection again, in the translation unit the plugin
  // is used in, from a description of what it is.
  std::string Desc, Error;
  if (!encodeConstevalPluginReflection(getContext(), RV, Desc, Error)) {
    EmitConstevalPluginUnsupported(
        Loc, "this reflection cannot be used in a consteval plugin: " + Error);
    return llvm::Constant::getNullValue(ConvertType(Ty));
  }
  llvm::Value *FnPtr = loadHostField(*this, CEHF_Reflect);
  auto *FTy = llvm::FunctionType::get(I64, {PtrTy, I64}, false);
  llvm::Value *H =
      Builder.CreateCall(FTy, FnPtr,
                         {CGM.GetAddrOfConstantCString(Desc).getPointer(),
                          llvm::ConstantInt::get(I64, Desc.size())});
  return Builder.CreateZExtOrTrunc(H, ConvertType(Ty));
}

llvm::Value *CodeGenFunction::EmitConstevalPluginMetaEqual(llvm::Value *LHS,
                                                           llvm::Value *RHS) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I1 = llvm::Type::getInt1Ty(LCtx);
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  llvm::Value *FnPtr = loadHostField(*this, CEHF_MetaEqual);
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
  llvm::Value *FnPtr = loadHostField(*this, CEHF_QueueInjection);
  auto *FTy = llvm::FunctionType::get(VoidTy, {I64, I64}, false);
  Builder.CreateCall(FTy, FnPtr, {NSVal, TSVal});
}

llvm::Value *CodeGenFunction::EmitConstevalPluginIdOrStrLit(
    bool IsStrLit, unsigned NumArgs, const Expr *const *Args,
    const Expr *const *SizeCalls, const Expr *const *DataCalls) {
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I1 = llvm::Type::getInt1Ty(LCtx);
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
      // (As ExtractStringFromArg in the constant evaluator.)
      uint32_t Kind = CEK_CharPtr;
      uint32_t Aux = 0;
      llvm::Value *Val = nullptr;
      llvm::Value *Len = nullptr;
      QualType ArgTy = Args[I]->getType();

      if (SizeCalls[I] && DataCalls[I]) {
        // String-like object (string_view, string): call data() and size().
        llvm::Value *DP = EmitScalarExpr(DataCalls[I]);
        llvm::Value *SZ = EmitScalarExpr(SizeCalls[I]);
        Val = Builder.CreatePtrToInt(DP, I64);
        Len = Builder.CreateZExtOrTrunc(SZ, I64);
        Aux = 1;
      } else if (ArgTy->isCharType()) {
        Kind = CEK_Char;
        Val = emitInt64(*this, Args[I]);
      } else if (ArgTy->isIntegralOrEnumerationType() &&
                 !ArgTy->isIncompleteType() &&
                 getContext().getTypeSize(ArgTy) <= 64) {
        Kind = CEK_Int;
        Aux = ArgTy->isUnsignedIntegerOrEnumerationType();
        Val = emitInt64(*this, Args[I]);
      } else if (ArgTy->isArrayType()) {
        // String literal or char[N]: emit lvalue address.
        llvm::Value *DP = EmitLValue(Args[I]).getPointer(*this);
        Val = Builder.CreatePtrToInt(DP, I64);
      } else if (ArgTy->isPointerType()) {
        // const char*
        llvm::Value *DP = EmitScalarExpr(Args[I]);
        Val = Builder.CreatePtrToInt(DP, I64);
      } else {
        EmitConstevalPluginUnsupported(
            Args[I]->getExprLoc(),
            "a value of type '" + ArgTy.getAsString() +
                "' cannot be part of a name or string literal in a consteval "
                "plugin yet");
        EmitIgnoredExpr(Args[I]);
        Kind = CEK_Void;
      }
      storeConstevalArgFields(*this, Slot, Kind, Aux, Val, Len);
    }
  }

  llvm::Value *FnPtr = loadHostField(*this, CEHF_MakeIdOrStrLit);
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
  llvm::Value *FnPtr = loadHostField(*this, CEHF_StringizeTokens);
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

  llvm::Value *FnPtr = loadHostField(*this, CEHF_ReportDiag);
  auto *FTy = llvm::FunctionType::get(VoidTy, {I32, PtrTy, I64, PtrTy, I64}, false);
  Builder.CreateCall(FTy, FnPtr, {Kind, TagPtr, TagLen, MsgPtr, MsgLen});
}

bool CodeGenFunction::isConstevalPluginMetaConstant(const VarDecl *VD) {
  if (!getLangOpts().ConstevalPlugin || isa<ParmVarDecl>(VD) ||
      VD->getType()->isReferenceType())
    return false;
  // One of this function's own variables is read where it is.
  if (VD->hasLocalStorage() && LocalDeclMap.count(VD))
    return false;
  if (!isOrContainsMetaType(getContext(), VD->getType()))
    return false;
  const VarDecl *Init = nullptr;
  if (!VD->getAnyInitializer(Init) || !Init ||
      !Init->isUsableInConstantExpressions(getContext()))
    return false;
  return Init->evaluateValue() != nullptr;
}

llvm::Value *
CodeGenFunction::tryEmitConstevalPluginInfoConstant(const Expr *E) {
  if (!E->getType()->isReflectionType())
    return nullptr;
  // A member or an element of such a constant, or all of it.
  const Expr *Base = E->IgnoreParenImpCasts();
  while (true) {
    if (const auto *ME = dyn_cast<MemberExpr>(Base); ME && !ME->isArrow())
      Base = ME->getBase()->IgnoreParenImpCasts();
    else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Base))
      Base = ASE->getBase()->IgnoreParenImpCasts();
    else
      break;
  }
  const auto *DRE = dyn_cast<DeclRefExpr>(Base);
  if (!DRE || (DRE->refersToEnclosingVariableOrCapture() &&
               DRE->isNonOdrUse() != NOUR_Constant))
    return nullptr;
  const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD || !isConstevalPluginMetaConstant(VD))
    return nullptr;
  Expr::EvalResult Result;
  if (!E->EvaluateAsRValue(Result, getContext()) || Result.HasSideEffects ||
      !Result.Val.isReflection())
    return nullptr;
  return EmitConstevalPluginReflection(Result.Val, E->getExprLoc(),
                                       E->getType());
}

void CodeGenFunction::EmitConstevalPluginConstant(const APValue &V, QualType T,
                                                  Address Dest,
                                                  SourceLocation Loc) {
  ASTContext &Ctx = getContext();
  T = T.getCanonicalType().getUnqualifiedType();
  auto Unsupported = [&] {
    EmitConstevalPluginUnsupported(
        Loc, "a constant of type '" + T.getAsString() +
                 "' cannot be used in a consteval plugin yet");
  };
  auto At = [&](CharUnits Offset, QualType FieldTy) {
    return Builder
        .CreateConstInBoundsByteGEP(Dest.withElementType(Int8Ty), Offset)
        .withElementType(ConvertTypeForMem(FieldTy));
  };

  if (!isOrContainsMetaType(Ctx, T)) {
    llvm::Constant *C = ConstantEmitter(*this).emitAbstract(Loc, V, T);
    C = CGM.getTypes().ConvertTypeForMem(T) == C->getType()
            ? C
            : ConstantEmitter(*this).emitForMemory(C, T);
    Builder.CreateStore(C, Dest.withElementType(C->getType()));
    return;
  }
  if (T->isReflectionType()) {
    if (!V.isReflection())
      return Unsupported();
    Builder.CreateStore(EmitConstevalPluginReflection(V, Loc, T),
                        Dest.withElementType(ConvertTypeForMem(T)));
    return;
  }
  if (T->isTokenSequenceType() || T->isMetaTokenType()) {
    if (!V.isTokenSequence())
      return Unsupported();
    llvm::Value *H = EmitConstevalPluginTokens(V.getTokenSequence());
    llvm::Type *MemTy = ConvertTypeForMem(T);
    Builder.CreateStore(Builder.CreateZExtOrTrunc(H, MemTy),
                        Dest.withElementType(MemTy));
    return;
  }
  if (const ConstantArrayType *CAT = Ctx.getAsConstantArrayType(T)) {
    if (!V.isArray())
      return Unsupported();
    QualType EltTy = CAT->getElementType();
    CharUnits EltSize = Ctx.getTypeSizeInChars(EltTy);
    for (uint64_t I = 0, N = CAT->getZExtSize(); I != N; ++I)
      EmitConstevalPluginConstant(I < V.getArrayInitializedElts()
                                      ? V.getArrayInitializedElt(I)
                                      : V.getArrayFiller(),
                                  EltTy, At(I * EltSize, EltTy), Loc);
    return;
  }
  if (const RecordDecl *RD = T->getAsRecordDecl()) {
    const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD);
    if (!V.isStruct() || RD->isUnion() ||
        (CXXRD && (CXXRD->isPolymorphic() || CXXRD->getNumVBases())))
      return Unsupported();
    const ASTRecordLayout &Layout = Ctx.getASTRecordLayout(RD);
    if (CXXRD) {
      unsigned I = 0;
      for (const CXXBaseSpecifier &B : CXXRD->bases()) {
        const CXXRecordDecl *BD = B.getType()->getAsCXXRecordDecl();
        const APValue &BV = V.getStructBase(I++);
        if (BD->isEmpty())
          continue;
        EmitConstevalPluginConstant(
            BV, B.getType(), At(Layout.getBaseClassOffset(BD), B.getType()),
            Loc);
      }
    }
    for (const FieldDecl *FD : RD->fields()) {
      if (FD->isUnnamedBitField())
        continue;
      if (FD->isBitField())
        return Unsupported();
      EmitConstevalPluginConstant(
          V.getStructField(FD->getFieldIndex()), FD->getType(),
          At(Ctx.toCharUnitsFromBits(
                 Layout.getFieldOffset(FD->getFieldIndex())),
             FD->getType()),
          Loc);
    }
    return;
  }
  Unsupported();
}

void CodeGenFunction::EmitConstevalPluginUnsupported(SourceLocation Loc,
                                                     const Twine &What) {
  SourceManager &SM = getContext().getSourceManager();
  if (Loc.isInvalid() || !SM.isInSystemHeader(Loc)) {
    CGM.Error(Loc, What.str());
    return;
  }
  // A plugin's translation unit has every consteval function of the headers
  // it includes; what one of those does that a plugin cannot is only a
  // problem for a plugin that calls it. Tell the host if that happens (it
  // does not return here).
  llvm::LLVMContext &LCtx = getLLVMContext();
  auto *I64 = llvm::Type::getInt64Ty(LCtx);
  auto *PtrTy = llvm::PointerType::getUnqual(LCtx);
  std::string Msg = What.str();
  PresumedLoc PLoc = SM.getPresumedLoc(Loc);
  if (PLoc.isValid())
    Msg += (" (" + llvm::sys::path::filename(PLoc.getFilename()) + ":" +
            Twine(PLoc.getLine()) + ")")
               .str();
  llvm::Value *FnPtr = loadHostField(*this, CEHF_Unsupported);
  auto *FTy =
      llvm::FunctionType::get(llvm::Type::getVoidTy(LCtx), {PtrTy, I64}, false);
  Builder.CreateCall(FTy, FnPtr,
                     {CGM.GetAddrOfConstantCString(Msg).getPointer(),
                      llvm::ConstantInt::get(I64, Msg.size())});
}
