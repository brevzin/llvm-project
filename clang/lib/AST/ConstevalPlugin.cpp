//===--- ConstevalPlugin.cpp - Native consteval plugin runtime --*- C++ -*-===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A plugin and the translation unit using it are separate compilations: what
// one means by a type, a declaration or a value has to be found again by the
// other. Everything here that crosses that boundary either arrives as what it
// was, or fails the call with the reason. Nothing is replaced by a null
// reflection, a zero, or an empty string.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ConstevalPlugin.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/MetaActions.h"
#include "clang/AST/Metafunction.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Reflection.h"
#include "clang/Basic/DiagnosticAST.h"
#include "clang/Basic/DiagnosticMetafn.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Version.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <csetjmp>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace clang;

ConstevalPluginHooks::~ConstevalPluginHooks() = default;

std::string clang::getConstevalPluginABI() {
  return getClangFullRepositoryVersion() + " consteval-plugin-abi 2";
}

//===----------------------------------------------------------------------===//
// What a type looks like where it crosses into a plugin
//===----------------------------------------------------------------------===//

static bool isPluginCharType(QualType T) {
  return T->isCharType() || T->isChar8Type();
}

bool clang::isOrContainsMetaType(ASTContext &Ctx, QualType T) {
  T = T.getCanonicalType();
  if (T->isReflectionType() || T->isTokenSequenceType() ||
      T->isMetaTokenType() || T->isStdMetaTokenSequenceType())
    return true;
  if (const ArrayType *AT = Ctx.getAsArrayType(T))
    return isOrContainsMetaType(Ctx, AT->getElementType());
  const RecordDecl *RD = T->getAsRecordDecl();
  if (!RD || !RD->isCompleteDefinition() || RD->isInvalidDecl())
    return false;
  if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD))
    for (const CXXBaseSpecifier &B : CXXRD->bases())
      if (isOrContainsMetaType(Ctx, B.getType()))
        return true;
  for (const FieldDecl *FD : RD->fields())
    if (isOrContainsMetaType(Ctx, FD->getType()))
      return true;
  return false;
}

/// The size a value of type \p T has in a plugin. That is its size here,
/// except for std::meta::token_sequence, which a consumer that has only
/// <meta_fwd> sees as an empty class.
static size_t pluginSizeOf(ASTContext &C, QualType T) {
  if (T->isStdMetaTokenSequenceType())
    T = C.TokenSequenceTy;
  return C.getTypeSizeInChars(T).getQuantity();
}

static void describeLayout(ASTContext &C, QualType T, llvm::raw_ostream &OS) {
  T = T.getCanonicalType();
  if (const auto *RT = T->getAs<ReferenceType>()) {
    QualType Pointee = RT->getPointeeType();
    OS << (isa<RValueReferenceType>(RT) ? "&&"
           : Pointee.isConstQualified() ? "&c"
                                        : "&");
    describeLayout(C, Pointee, OS);
    return;
  }
  T = T.getUnqualifiedType();
  if (T->isVoidType()) {
    OS << 'v';
  } else if (T->isReflectionType()) {
    OS << 'I';
  } else if (T->isTokenSequenceType()) {
    OS << 'S';
  } else if (T->isMetaTokenType()) {
    OS << 'T';
  } else if (T->isStdMetaTokenSequenceType()) {
    OS << 'Q';
  } else if (T->isNullPtrType()) {
    OS << 'n';
  } else if (T->isBooleanType()) {
    OS << 'b';
  } else if (T->isIncompleteType()) {
    OS << '?';
  } else if (T->isIntegralOrEnumerationType()) {
    OS << 'i' << C.getTypeSize(T)
       << (T->isUnsignedIntegerOrEnumerationType() ? 'u' : 's');
  } else if (T->isRealFloatingType()) {
    OS << 'f' << C.getTypeSize(T);
  } else if (T->isPointerType()) {
    QualType Pointee = T->getPointeeType();
    if (isPluginCharType(Pointee))
      OS << 's' << C.getTypeSize(Pointee)
         << (Pointee.isConstQualified() ? 'c' : 'm');
    else
      OS << 'p';
  } else if (T->isMemberPointerType()) {
    OS << 'm';
  } else if (const ConstantArrayType *CAT = C.getAsConstantArrayType(T)) {
    OS << 'A' << CAT->getZExtSize() << '[';
    describeLayout(C, CAT->getElementType(), OS);
    OS << ']';
  } else if (const RecordDecl *RD = T->getAsRecordDecl()) {
    if (RD->isInvalidDecl()) {
      OS << '?';
      return;
    }
    const ASTRecordLayout &Layout = C.getASTRecordLayout(RD);
    OS << (RD->isUnion() ? 'U' : '{') << Layout.getSize().getQuantity() << ':';
    if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      if (CXXRD->isPolymorphic() || CXXRD->getNumVBases())
        OS << 'V';
      for (const CXXBaseSpecifier &B : CXXRD->bases()) {
        const CXXRecordDecl *BD = B.getType()->getAsCXXRecordDecl();
        if (B.isVirtual() || !BD)
          continue;
        OS << 'B' << Layout.getBaseClassOffset(BD).getQuantity() << '=';
        describeLayout(C, B.getType(), OS);
        OS << ';';
      }
    }
    for (const FieldDecl *FD : RD->fields()) {
      if (FD->isUnnamedBitField())
        continue;
      OS << Layout.getFieldOffset(FD->getFieldIndex());
      if (FD->isBitField())
        OS << 'w' << FD->getBitWidthValue();
      OS << '=';
      describeLayout(C, FD->getType(), OS);
      OS << ';';
    }
    if (!RD->isUnion())
      OS << '}';
  } else {
    OS << '?' << T->getTypeClassName();
  }
}

std::string clang::getConstevalPluginSignature(ASTContext &Ctx,
                                               const FunctionDecl *FD) {
  std::string Sig;
  llvm::raw_string_ostream OS(Sig);
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD);
      MD && MD->isImplicitObjectMemberFunction()) {
    OS << (MD->isConst() ? "this&c" : "this&");
    describeLayout(Ctx, Ctx.getCanonicalTagType(MD->getParent()), OS);
  }
  OS << '(';
  for (const ParmVarDecl *PVD : FD->parameters()) {
    describeLayout(Ctx, PVD->getType(), OS);
    OS << ',';
  }
  if (FD->isVariadic())
    OS << "...";
  OS << ")->";
  describeLayout(Ctx, FD->getReturnType(), OS);
  return Sig;
}

//===----------------------------------------------------------------------===//
// Describing a reflection, to find it again in another translation unit
//===----------------------------------------------------------------------===//
//
//   reflection := '0'                             the null reflection
//               | 'G'                             the global namespace
//               | 'N' kind scope name [type]      a named entity
//               | 'S' reflection NUM ';' targ*    a template specialization
//               | type
//   kind       := 'n' namespace (or alias) | 't' type (or alias) | 'T' template
//               | 'v' variable, data member, enumerator
//               | 'f' function (followed by its type)
//   scope      := 'G' | reflection of a namespace or class
//   name       := 'I' NUM ';' bytes   identifier
//               | 'O' NUM ';'         operator function
//               | 'L' NUM ';' bytes   literal operator
//               | 'C' type            conversion function
//               | 'c' | 'd'           constructor, destructor
//   type       := 'b' NUM ';'                      builtin (BuiltinType::Kind)
//               | 'Q' NUM ';' type                 cv-qualified
//               | 'P' type | 'L' type | 'R' type   pointer, lvalue/rvalue ref
//               | 'A' NUM ';' type | 'U' type      array of N / unknown bound
//               | 'F' NUM ';' NUM ';' NUM ';' type NUM ';' type*
//                     function: cv, ref-qualifier, flags, result, parameters
//               | 'M' type type                    pointer to member of class
//               | 'N' 't' ... | 'S' ...
//   targ       := 't' type | 'i' type INT ';' | 'T' reflection
//               | 'd' reflection | 'n' type
//
// A type is described by what it is (its canonical type), except that the
// reflection of an alias is of the alias.

namespace {

class ReflectionEncoder {
  ASTContext &C;
  std::string &Out;
  std::string &Error;

  bool fail(const llvm::Twine &Why) {
    if (Error.empty())
      Error = Why.str();
    return false;
  }
  void num(uint64_t N) {
    Out += std::to_string(N);
    Out += ';';
  }
  void str(StringRef S) {
    num(S.size());
    Out += S;
  }

  bool scope(const NamedDecl *Of) {
    // (Lookup in a namespace finds the members of its inline namespaces.)
    const DeclContext *DC = Of->getDeclContext()->getRedeclContext();
    while (DC->isInlineNamespace())
      DC = DC->getParent()->getRedeclContext();
    if (isa<TranslationUnitDecl>(DC)) {
      Out += 'G';
      return true;
    }
    if (const auto *NS = dyn_cast<NamespaceDecl>(DC)) {
      if (NS->isAnonymousNamespace())
        return fail("'" + Of->getQualifiedNameAsString() +
                    "' is declared in an unnamed namespace, so another "
                    "translation unit cannot name it");
      return named('n', NS);
    }
    if (const auto *TD = dyn_cast<TagDecl>(DC))
      return type(C.getCanonicalTagType(TD));
    return fail("'" + Of->getQualifiedNameAsString() +
                "' is local to a function, so another translation unit "
                "cannot name it");
  }

  bool name(const NamedDecl *D) {
    DeclarationName N = D->getDeclName();
    switch (N.getNameKind()) {
    case DeclarationName::Identifier:
      if (!N.getAsIdentifierInfo())
        return fail("an unnamed " + StringRef(D->getDeclKindName()) +
                    " declaration cannot be named from another translation "
                    "unit");
      Out += 'I';
      str(N.getAsIdentifierInfo()->getName());
      return true;
    case DeclarationName::CXXOperatorName:
      Out += 'O';
      num(N.getCXXOverloadedOperator());
      return true;
    case DeclarationName::CXXLiteralOperatorName:
      Out += 'L';
      str(N.getCXXLiteralIdentifier()->getName());
      return true;
    case DeclarationName::CXXConversionFunctionName:
      Out += 'C';
      return type(N.getCXXNameType());
    case DeclarationName::CXXConstructorName:
      Out += 'c';
      return true;
    case DeclarationName::CXXDestructorName:
      Out += 'd';
      return true;
    default:
      return fail("'" + D->getQualifiedNameAsString() +
                  "' has a name that cannot be described");
    }
  }

  bool named(char Kind, const NamedDecl *D) {
    Out += 'N';
    Out += Kind;
    return scope(D) && name(D);
  }

  bool templateArgs(ArrayRef<TemplateArgument> Args) {
    SmallVector<TemplateArgument, 8> Flat;
    std::function<void(ArrayRef<TemplateArgument>)> Flatten =
        [&](ArrayRef<TemplateArgument> As) {
          for (const TemplateArgument &A : As) {
            if (A.getKind() == TemplateArgument::Pack)
              Flatten(A.getPackAsArray());
            else
              Flat.push_back(A);
          }
        };
    Flatten(Args);
    num(Flat.size());
    for (const TemplateArgument &A : Flat) {
      switch (A.getKind()) {
      case TemplateArgument::Type:
        Out += 't';
        if (!type(A.getAsType()))
          return false;
        break;
      case TemplateArgument::Integral:
        Out += 'i';
        if (!type(A.getIntegralType()))
          return false;
        Out += toString(A.getAsIntegral(), 10);
        Out += ';';
        break;
      case TemplateArgument::Template:
        Out += 'T';
        if (TemplateDecl *TD = A.getAsTemplate().getAsTemplateDecl()) {
          if (!templ(TD))
            return false;
          break;
        }
        return fail("a template argument that is not a template declaration "
                    "cannot be described");
      case TemplateArgument::Declaration:
        Out += 'd';
        if (!decl(A.getAsDecl()) || !type(A.getParamTypeForDecl()))
          return false;
        break;
      case TemplateArgument::NullPtr:
        Out += 'n';
        if (!type(A.getNullPtrType()))
          return false;
        break;
      case TemplateArgument::Expression: {
        // (As written, in an alias template specialization.)
        Expr *E = A.getAsExpr();
        std::optional<llvm::APSInt> V;
        if (!E->isValueDependent() &&
            E->getType()->isIntegralOrEnumerationType())
          V = E->getIntegerConstantExpr(C);
        if (!V)
          return fail("a template argument that is not a type, an integer, a "
                      "declaration or a template cannot be described yet");
        Out += 'i';
        if (!type(E->getType()))
          return false;
        Out += toString(*V, 10);
        Out += ';';
        break;
      }
      default:
        return fail("a template argument that is not a type, an integer, a "
                    "declaration or a template cannot be described yet");
      }
    }
    return true;
  }

  bool templ(const TemplateDecl *TD) {
    if (isa<TemplateTemplateParmDecl>(TD))
      return fail("a template template parameter cannot be described");
    return named('T', TD);
  }

  bool tag(const TagDecl *TD) {
    if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(TD)) {
      if (isa<ClassTemplatePartialSpecializationDecl>(Spec))
        return fail("a partial specialization cannot be described");
      Out += 'S';
      return templ(Spec->getSpecializedTemplate()) &&
             templateArgs(Spec->getTemplateArgs().asArray());
    }
    if (const auto *RD = dyn_cast<CXXRecordDecl>(TD); RD && RD->isLambda())
      return fail("a closure type cannot be named from another translation "
                  "unit");
    if (!TD->getIdentifier())
      return fail("an unnamed class or enumeration cannot be named from "
                  "another translation unit");
    return named('t', TD);
  }

  bool decl(const ValueDecl *VD) {
    if (const auto *FD = dyn_cast<FunctionDecl>(VD)) {
      if (const FunctionTemplateDecl *Primary = FD->getPrimaryTemplate()) {
        Out += 'S';
        return templ(Primary) &&
               templateArgs(FD->getTemplateSpecializationArgs()->asArray());
      }
      return named('f', FD) && type(FD->getType());
    }
    if (const auto *Spec = dyn_cast<VarTemplateSpecializationDecl>(VD)) {
      if (isa<VarTemplatePartialSpecializationDecl>(Spec))
        return fail("a partial specialization cannot be described");
      Out += 'S';
      return templ(Spec->getSpecializedTemplate()) &&
             templateArgs(Spec->getTemplateArgs().asArray());
    }
    if (!isa<VarDecl, FieldDecl, EnumConstantDecl>(VD) || isa<ParmVarDecl>(VD))
      return fail("a reflection of a " + StringRef(VD->getDeclKindName()) +
                  " declaration cannot be described yet");
    return named('v', VD);
  }

public:
  ReflectionEncoder(ASTContext &C, std::string &Out, std::string &Error)
      : C(C), Out(Out), Error(Error) {}

  /// The type \p T itself: whatever names it was given are looked through.
  bool type(QualType T) {
    T = C.getCanonicalType(T);
    Qualifiers Quals;
    T = C.getUnqualifiedArrayType(T, Quals);
    if (Quals.hasNonFastQualifiers())
      return fail("type '" + QualType(T).getAsString() +
                  "' has qualifiers that cannot be described");
    if (unsigned CVR = Quals.getCVRQualifiers()) {
      Out += 'Q';
      num(CVR);
    }
    switch (T->getTypeClass()) {
    case Type::Builtin:
      Out += 'b';
      num(cast<BuiltinType>(T)->getKind());
      return true;
    case Type::Pointer:
      Out += 'P';
      return type(T->getPointeeType());
    case Type::LValueReference:
      Out += 'L';
      return type(T->getPointeeType());
    case Type::RValueReference:
      Out += 'R';
      return type(T->getPointeeType());
    case Type::ConstantArray:
      Out += 'A';
      num(cast<ConstantArrayType>(T)->getZExtSize());
      return type(cast<ArrayType>(T)->getElementType());
    case Type::IncompleteArray:
      Out += 'U';
      return type(cast<ArrayType>(T)->getElementType());
    case Type::FunctionProto: {
      const auto *FPT = cast<FunctionProtoType>(T);
      Out += 'F';
      num(FPT->getMethodQuals().getCVRQualifiers());
      num(FPT->getRefQualifier());
      num((FPT->isVariadic() ? 1 : 0) | (FPT->isNothrow() ? 2 : 0));
      if (!type(FPT->getReturnType()))
        return false;
      num(FPT->getNumParams());
      for (QualType P : FPT->param_types())
        if (!type(P))
          return false;
      return true;
    }
    case Type::MemberPointer: {
      const auto *MPT = cast<MemberPointerType>(T);
      Out += 'M';
      return type(C.getCanonicalTagType(MPT->getMostRecentCXXRecordDecl())) &&
             type(MPT->getPointeeType());
    }
    case Type::Record:
    case Type::Enum:
      return tag(T->getAsTagDecl());
    default:
      return fail("type '" + QualType(T).getAsString() +
                  "' cannot be described yet");
    }
  }

  bool reflection(const APValue &RV) {
    switch (RV.getReflectionKind()) {
    case ReflectionKind::Null:
      Out += '0';
      return true;
    case ReflectionKind::Type: {
      QualType QT = RV.getReflectedType();
      // The reflection of an alias is of the alias, not of the type.
      if (QT->isTypedefNameType()) {
        if (QT.hasLocalNonFastQualifiers())
          return fail("type '" + QT.getAsString() +
                      "' has qualifiers that cannot be described");
        if (unsigned CVR = QT.getLocalCVRQualifiers()) {
          Out += 'Q';
          num(CVR);
        }
        if (const auto *TDT = dyn_cast<TypedefType>(QT.getTypePtr()))
          return named('t', TDT->getDecl());
        if (const auto *TST =
                dyn_cast<TemplateSpecializationType>(QT.getTypePtr())) {
          TemplateDecl *TD = TST->getTemplateName().getAsTemplateDecl();
          if (!TD)
            return fail("type '" + QT.getAsString() +
                        "' cannot be described yet");
          Out += 'S';
          return templ(TD) && templateArgs(TST->template_arguments());
        }
        return fail("type '" + QT.getAsString() + "' cannot be described yet");
      }
      return type(QT);
    }
    case ReflectionKind::Declaration:
      return decl(RV.getReflectedDecl());
    case ReflectionKind::Template:
      if (TemplateDecl *TD = RV.getReflectedTemplate().getAsTemplateDecl())
        return templ(TD);
      return fail("a reflection of a template that is not a template "
                  "declaration cannot be described");
    case ReflectionKind::Namespace: {
      const Decl *D = RV.getReflectedNamespace();
      if (isa<TranslationUnitDecl>(D)) {
        Out += 'G';
        return true;
      }
      const auto *ND = cast<NamedDecl>(D);
      if (const auto *NS = dyn_cast<NamespaceDecl>(ND);
          NS && NS->isAnonymousNamespace())
        return fail("an unnamed namespace cannot be named from another "
                    "translation unit");
      return named('n', ND);
    }
    default:
      return fail("only reflections of types, declarations, templates and "
                  "namespaces can be described yet");
    }
  }
};

} // namespace

bool clang::encodeConstevalPluginReflection(ASTContext &Ctx, const APValue &RV,
                                            std::string &Desc,
                                            std::string &Error) {
  Desc.clear();
  Error.clear();
  if (!RV.isReflection()) {
    Error = "not a reflection";
    return false;
  }
  return ReflectionEncoder(Ctx, Desc, Error).reflection(RV);
}

//===----------------------------------------------------------------------===//
// The plugins loaded for a translation unit
//===----------------------------------------------------------------------===//

namespace {

struct ActiveHostState;

struct PluginThunk {
  ConstevalThunkFn Fn = nullptr;
  /// The signature the plugin recorded for the function.
  const char *Signature = nullptr;
  /// Whether the signature has been checked against the declaration here,
  /// and what was wrong with it if anything.
  bool Checked = false;
  std::string Mismatch;
};

struct PluginState {
  std::vector<llvm::sys::DynamicLibrary> Plugins;
  llvm::DenseMap<const FunctionDecl *, std::unique_ptr<PluginThunk>> Thunks;
  /// What stands for std::meta::__range_of_infos::sentinel where the
  /// consumer has not declared it (see ReflectionDecoder::fallback).
  QualType Sentinel;
};

extern const ConstevalHostAPI GlobalHostAPI;

std::mutex StatesMutex;
llvm::DenseMap<const ASTContext *, std::unique_ptr<PluginState>> &states() {
  static llvm::DenseMap<const ASTContext *, std::unique_ptr<PluginState>> S;
  return S;
}

PluginState &getPluginState(ASTContext &Ctx) {
  std::lock_guard<std::mutex> Lock(StatesMutex);
  std::unique_ptr<PluginState> &Slot = states()[&Ctx];
  if (Slot)
    return *Slot;
  Slot = std::make_unique<PluginState>();
  Ctx.AddDeallocation(
      [](void *P) {
        std::lock_guard<std::mutex> Lock(StatesMutex);
        states().erase(static_cast<const ASTContext *>(P));
      },
      &Ctx);

  PluginState &S = *Slot;
  const std::string ABI = getConstevalPluginABI();
  for (const std::string &Path : Ctx.getLangOpts().ConstevalPluginPaths) {
    auto Fail = [&](StringRef Why) {
      Ctx.getDiagnostics().Report(diag::err_consteval_plugin_load)
          << Path << Why;
    };
    // (A reflection is passed to a plugin as a handle, which needs the room.)
    if (Ctx.getTypeSize(Ctx.MetaInfoTy) < 64) {
      Fail("plugins cannot be used with -fconsteval-operations");
      continue;
    }
    std::string Err;
    auto Lib =
        llvm::sys::DynamicLibrary::getPermanentLibrary(Path.c_str(), &Err);
    if (!Lib.isValid()) {
      Fail(Err);
      continue;
    }
    const auto *PluginABI = static_cast<const char *>(
        Lib.getAddressOfSymbol(ConstevalPluginABISymbol));
    void *Init = Lib.getAddressOfSymbol(ConstevalPluginInitSymbol);
    if (!PluginABI || !Init) {
      Fail("it was not built with -fconsteval-plugin");
      continue;
    }
    if (ABI != PluginABI) {
      Fail(("it was built by a different compiler ('" + StringRef(PluginABI) +
            "'; this is '" + ABI + "')")
               .str());
      continue;
    }
    using InitFn = void (*)(const ConstevalHostAPI *);
    reinterpret_cast<InitFn>(Init)(&GlobalHostAPI);
    S.Plugins.push_back(Lib);
  }
  return S;
}

PluginThunk *lookupPluginThunk(ASTContext &Ctx, const FunctionDecl *FD) {
  if (Ctx.getLangOpts().ConstevalPluginPaths.empty())
    return nullptr;
  PluginState &S = getPluginState(Ctx);
  if (S.Plugins.empty())
    return nullptr;
  FD = FD->getCanonicalDecl();
  std::unique_ptr<PluginThunk> &Slot = S.Thunks[FD];
  if (Slot)
    return Slot->Fn ? Slot.get() : nullptr;
  Slot = std::make_unique<PluginThunk>();

  std::unique_ptr<MangleContext> MC(Ctx.createMangleContext());
  SmallString<128> Mangled;
  llvm::raw_svector_ostream OS(Mangled);
  MC->mangleName(FD, OS);
  std::string ThunkName = (ConstevalPluginThunkPrefix + Mangled).str();
  std::string SigName = (ConstevalPluginSignaturePrefix + Mangled).str();
  // (The first plugin that defines the function is the one used.)
  for (llvm::sys::DynamicLibrary &Lib : S.Plugins) {
    if (void *Sym = Lib.getAddressOfSymbol(ThunkName.c_str())) {
      Slot->Fn = reinterpret_cast<ConstevalThunkFn>(Sym);
      Slot->Signature =
          static_cast<const char *>(Lib.getAddressOfSymbol(SigName.c_str()));
      return Slot.get();
    }
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// One invocation
//===----------------------------------------------------------------------===//

/// An array of characters the plugin can see, and where it came from.
struct CharBuffer {
  const char *Data;
  size_t Size;
  /// A pointer to the first element of the array here.
  APValue First;
  QualType CharTy;
};

/// Active host state for the currently executing consteval plugin call.
struct ActiveHostState {
  ASTContext &Ctx;
  MetaActions &Meta;
  const FunctionDecl *FD;
  SourceLocation CallLoc;
  Decl *ContainingDecl;
  bool AllowInjection;
  Expr::EvalStatus *EvalStatus;
  ConstevalPluginHooks &Hooks;
  ConstevalPluginFailure &Failure;
  bool Failed = false;
  std::jmp_buf ErrorJump;

  // Handle arenas: handle 0 is null reflection / empty token sequence.
  // Handle i (i >= 1) indexes into slot (i - 1).
  std::vector<APValue> Reflections;
  std::vector<TokenSequenceData> TokenSeqs;
  std::vector<CharBuffer> CharBuffers;
  /// The reflections already found for the plugin's reflect-expressions,
  /// by description.
  llvm::DenseMap<const char *, uint64_t> Described;
  llvm::BumpPtrAllocator ScratchAlloc;

  ActiveHostState(ASTContext &Ctx, MetaActions &Meta, const FunctionDecl *FD,
                  SourceLocation CallLoc, Decl *ContainingDecl,
                  bool AllowInjection, Expr::EvalStatus *EvalStatus,
                  ConstevalPluginHooks &Hooks, ConstevalPluginFailure &Failure)
      : Ctx(Ctx), Meta(Meta), FD(FD), CallLoc(CallLoc),
        ContainingDecl(ContainingDecl), AllowInjection(AllowInjection),
        EvalStatus(EvalStatus), Hooks(Hooks), Failure(Failure) {}

  SourceRange range() const { return SourceRange(CallLoc, CallLoc); }

  /// Fail the invocation, for the reason \p Why (the first reason given is
  /// the one reported). Returns false.
  bool fail(const llvm::Twine &Why) {
    if (!Failed) {
      Failed = true;
      Failure.Message = Why.str();
    }
    return false;
  }
  /// Fail the invocation; what was noted says why.
  bool fail() {
    Failed = true;
    return false;
  }
  PartialDiagnostic &note(unsigned DiagID) { return note(CallLoc, DiagID); }
  PartialDiagnostic &note(SourceLocation Loc, unsigned DiagID) {
    return Failure.Notes
        .emplace_back(std::piecewise_construct, std::make_tuple(Loc),
                      std::make_tuple(DiagID, std::ref(Ctx.getDiagAllocator())))
        .second;
  }

  uint64_t internReflection(const APValue &V) {
    if (V.isReflection() && V.getReflectionKind() == ReflectionKind::Null)
      return 0;
    Reflections.push_back(V);
    return Reflections.size();
  }

  bool lookupReflection(uint64_t Handle, APValue &Out) {
    if (Handle == 0) {
      Out = APValue(ReflectionKind::Null, nullptr);
      return true;
    }
    if (Handle > Reflections.size())
      return fail("the plugin passed something that is not a reflection as "
                  "one");
    Out = Reflections[Handle - 1];
    return true;
  }

  uint64_t internTokenSeq(TokenSequenceData TSD) {
    if (TSD.empty())
      return 0;
    TokenSeqs.push_back(TSD);
    return TokenSeqs.size();
  }

  bool lookupTokenSeq(uint64_t Handle, TokenSequenceData &Out) {
    if (Handle == 0) {
      Out = CreateEmptyTokenSequenceData(Ctx);
      return true;
    }
    if (Handle > TokenSeqs.size())
      return fail("the plugin passed something that is not a token sequence "
                  "as one");
    Out = TokenSeqs[Handle - 1];
    return true;
  }

  const char *copyString(llvm::StringRef S) {
    char *Buf = Ctx.Allocate<char>(S.size() + 1);
    if (!S.empty())
      std::memcpy(Buf, S.data(), S.size());
    Buf[S.size()] = '\0';
    return Buf;
  }

  /// The plugin's view of the characters \p Ptr points into: a pointer to
  /// the same character in a copy of the whole array (the same copy for
  /// every pointer into that array). Null if they cannot be read.
  const char *exportChars(const APValue &Ptr, QualType CharTy) {
    SmallString<64> Bytes;
    uint64_t Index = 0;
    APValue First;
    if (!Hooks.readCharArray(Ptr, CharTy, Bytes, Index, First))
      return nullptr;
    llvm::FoldingSetNodeID ID;
    First.Profile(ID);
    for (const CharBuffer &B : CharBuffers) {
      llvm::FoldingSetNodeID BID;
      B.First.Profile(BID);
      if (BID == ID && B.Size == Bytes.size())
        return B.Data + Index;
    }
    // (One more than the array, so that a string that is not terminated
    // where the plugin expects it to be still ends.)
    char *Data = ScratchAlloc.Allocate<char>(Bytes.size() + 1);
    std::memcpy(Data, Bytes.data(), Bytes.size());
    Data[Bytes.size()] = '\0';
    CharBuffers.push_back({Data, Bytes.size(), First, CharTy});
    return Data + Index;
  }

  /// The pointer here for the plugin's pointer \p P to characters: into the
  /// array it was exported from if it is one of those, otherwise to a copy
  /// of the \p Len characters there (the string there, if \p Len is not
  /// given).
  bool importChars(const char *P, std::optional<size_t> Len, QualType CharTy,
                   APValue &Out) {
    if (!P) {
      Out = APValue(APValue::LValueBase(), CharUnits::Zero(),
                    APValue::NoLValuePath(), /*IsNullPtr=*/true);
      return true;
    }
    for (const CharBuffer &B : CharBuffers)
      if (P >= B.Data && P <= B.Data + B.Size)
        return Hooks.offsetCharPointer(B.First, B.CharTy, P - B.Data, Out) ||
               fail("a pointer the plugin returned could not be formed here");
    llvm::StringRef SR(P, Len ? *Len : std::strlen(P));
    QualType EltTy = CharTy.getUnqualifiedType();
    auto *SL = StringLiteral::Create(
        Ctx, SR,
        EltTy->isChar8Type() ? StringLiteralKind::UTF8
                             : StringLiteralKind::Ordinary,
        /*Pascal=*/false, Ctx.getStringLiteralArrayType(EltTy, SR.size()),
        CallLoc);
    APValue::LValuePathEntry Path[1] = {
        APValue::LValuePathEntry::ArrayIndex(0)};
    Out = APValue(SL, CharUnits::Zero(), Path, /*OnePastTheEnd=*/false);
    return true;
  }
};

thread_local ActiveHostState *CurrentHost = nullptr;

/// The state of the invocation a plugin is calling back from.
ActiveHostState &currentHost() {
  if (!CurrentHost)
    llvm::report_fatal_error(
        "a consteval plugin called into the compiler while none of its "
        "functions was being evaluated (from the initializer of a variable "
        "with static storage duration?)");
  return *CurrentHost;
}

/// Abandon the invocation: control returns to invokeConstevalPlugin, not to
/// the plugin. Called only with nothing of the compiler's left to destroy on
/// the way (the plugin's own frames are simply dropped).
[[noreturn]] void abandon(ActiveHostState &H) {
  H.Failed = true;
  std::longjmp(H.ErrorJump, 1);
}

/// Helper: wrap an APValue in a ConstantExpr so Metafunction::evaluate can read
/// it through its standard Expr* interface.
Expr *makeConstArgExpr(ASTContext &C, SourceLocation Loc, const APValue &V,
                       QualType T) {
  auto *OVE = new (C) OpaqueValueExpr(Loc, T, VK_PRValue);
  return ConstantExpr::Create(C, OVE, V);
}

/// Evaluate an argument made by makeConstArgExpr -- or an element of an array
/// of reflections that is such an argument, which is either an array here
/// or the address of the plugin's array of handles.
bool hostEvaluate(ActiveHostState &H, APValue &Result, const Expr *E) {
  ASTContext &C = H.Ctx;
  if (const auto *CE = dyn_cast<ConstantExpr>(E)) {
    Result = CE->getAPValueResult();
    return true;
  }
  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    const auto *BaseCE = dyn_cast<ConstantExpr>(ASE->getBase());
    const auto *IdxLit = dyn_cast<IntegerLiteral>(ASE->getIdx());
    if (BaseCE && IdxLit) {
      const APValue &Base = BaseCE->getAPValueResult();
      uint64_t Idx = IdxLit->getValue().getZExtValue();
      if (Base.isArray()) {
        if (Idx >= Base.getArraySize())
          return false;
        Result = Base.getArrayInitializedElt(Idx);
        return true;
      }
      if (Base.isInt()) {
        size_t Stride = C.getTypeSizeInChars(C.MetaInfoTy).getQuantity();
        uint64_t Handle = 0;
        const auto *Ptr = reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(Base.getInt().getZExtValue()));
        std::memcpy(&Handle, Ptr + Idx * Stride,
                    std::min(Stride, sizeof(Handle)));
        return H.lookupReflection(Handle, Result);
      }
    }
  }
  Expr::EvalResult ER;
  if (E->EvaluateAsRValue(ER, C)) {
    Result = std::move(ER.Val);
    return true;
  }
  return false;
}

Metafunction::EvaluateFn hostEvaluator(ActiveHostState &H) {
  return [&H](APValue &Result, const Expr *E, unsigned /*ConvertToRValue*/) {
    return hostEvaluate(H, Result, E);
  };
}

Metafunction::DiagnoseFn hostDiagnoser(ActiveHostState &H) {
  return [&H](SourceLocation Loc, unsigned DiagID) -> PartialDiagnostic & {
    return H.note(Loc, DiagID);
  };
}

//===----------------------------------------------------------------------===//
// Finding a described reflection
//===----------------------------------------------------------------------===//

class ReflectionDecoder {
  ActiveHostState &H;
  ASTContext &C;
  StringRef S;
  size_t Pos = 0;

  struct Scope {
    /// Null if a namespace on the way is not declared here.
    DeclContext *DC = nullptr;
    /// Its qualified name (empty for the global namespace).
    std::string Name;
  };

  bool malformed() { return H.fail("malformed reflection description"); }
  bool atEnd() const { return Pos >= S.size(); }
  char peek() const { return atEnd() ? '\0' : S[Pos]; }
  char next() { return atEnd() ? '\0' : S[Pos++]; }
  bool num(uint64_t &N) {
    size_t End = S.find(';', Pos);
    if (End == StringRef::npos || S.substr(Pos, End - Pos).getAsInteger(10, N))
      return malformed();
    Pos = End + 1;
    return true;
  }
  bool str(StringRef &Out) {
    uint64_t Len;
    if (!num(Len))
      return false;
    if (Len > S.size() - Pos)
      return malformed();
    Out = S.substr(Pos, Len);
    Pos += Len;
    return true;
  }

  bool notDeclared(StringRef QualName) {
    return H.fail("the plugin refers to '" + QualName +
                  "', which is not declared in this translation unit");
  }

  static std::string qualify(const Scope &Sc, StringRef Name) {
    return Sc.Name.empty() ? Name.str() : Sc.Name + "::" + Name.str();
  }

  bool builtin(uint64_t Kind, QualType &T) {
    switch (static_cast<BuiltinType::Kind>(Kind)) {
#define BUILTIN_TYPE(Id, SingletonId)                                          \
  case BuiltinType::Id:                                                        \
    T = C.SingletonId;                                                         \
    break;
#include "clang/AST/BuiltinTypes.def"
    default:
      T = QualType();
      break;
    }
    return !T.isNull() || H.fail("the plugin refers to a builtin type that "
                                 "this compilation does not have");
  }

  /// The definition of the class \p T, to look a member up in.
  bool classScope(QualType T, Scope &Sc) {
    TagDecl *TD = T->getAsTagDecl();
    if (!TD)
      return malformed();
    Sc.Name = T.getAsString(C.getPrintingPolicy());
    if (auto *RD = dyn_cast<CXXRecordDecl>(TD)) {
      if (!RD->getDefinition() && !H.Meta.EnsureInstantiated(RD, H.range()))
        return H.fail();
      if (!RD->getDefinition())
        return H.fail("the plugin refers to a member of '" + Sc.Name +
                      "', which is incomplete in this translation unit");
      RD = RD->getDefinition();
      H.Meta.EnsureDeclarationOfImplicitMembers(RD);
      Sc.DC = RD;
      return true;
    }
    if (TagDecl *Def = TD->getDefinition())
      TD = Def;
    Sc.DC = TD;
    return true;
  }

  bool scope(Scope &Sc) {
    if (peek() == 'G') {
      ++Pos;
      Sc.DC = C.getTranslationUnitDecl();
      return true;
    }
    if (peek() == 'N' && Pos + 1 < S.size() && S[Pos + 1] == 'n') {
      Pos += 2;
      Scope Parent;
      StringRef Name;
      if (!scope(Parent))
        return false;
      if (next() != 'I')
        return malformed();
      if (!str(Name))
        return false;
      Sc.Name = qualify(Parent, Name);
      if (!Parent.DC)
        return true;
      for (NamedDecl *D : Parent.DC->lookup(&C.Idents.get(Name))) {
        if (auto *NS = dyn_cast<NamespaceDecl>(D->getUnderlyingDecl()))
          Sc.DC = NS;
        if (Sc.DC)
          break;
      }
      return true;
    }
    QualType T;
    return type(T) && classScope(T, Sc);
  }

  /// Where the consumer has not declared them, the two entities <meta> uses
  /// of its own: anything distinct from every other type will do for the
  /// first, and std::type_identity has the member the second is used for.
  bool fallback(char Kind, StringRef QualName, APValue &RV) {
    if (Kind == 't' && QualName == "std::meta::__range_of_infos::sentinel") {
      PluginState &PS = getPluginState(C);
      if (PS.Sentinel.isNull()) {
        auto *RD = CXXRecordDecl::Create(
            C, TagTypeKind::Struct, C.getTranslationUnitDecl(),
            SourceLocation(), SourceLocation(),
            &C.Idents.get("__consteval_plugin_sentinel"));
        RD->setImplicit();
        RD->startDefinition();
        RD->completeDefinition();
        PS.Sentinel = C.getCanonicalTagType(RD);
      }
      RV = APValue(ReflectionKind::Type, PS.Sentinel.getAsOpaquePtr());
      return true;
    }
    if (Kind == 'T' && QualName == "std::meta::detail::__wrap_workaround") {
      for (NamedDecl *StdD :
           C.getTranslationUnitDecl()->lookup(&C.Idents.get("std"))) {
        auto *Std = dyn_cast<NamespaceDecl>(StdD);
        if (!Std)
          continue;
        for (NamedDecl *D : Std->lookup(&C.Idents.get("type_identity")))
          if (auto *TD = dyn_cast<ClassTemplateDecl>(D->getUnderlyingDecl())) {
            RV = APValue(
                ReflectionKind::Template,
                TemplateName(TD->getCanonicalDecl()).getAsVoidPointer());
            return true;
          }
      }
    }
    return false;
  }

  bool named(APValue &RV) {
    char Kind = next();
    Scope Sc;
    if (!scope(Sc))
      return false;

    DeclarationName Name;
    std::string Spelling;
    QualType ClassTy;
    if (auto *TD = dyn_cast_or_null<TagDecl>(Sc.DC))
      ClassTy = C.getCanonicalTagType(TD);
    switch (next()) {
    case 'I': {
      StringRef Id;
      if (!str(Id))
        return false;
      Name = &C.Idents.get(Id);
      Spelling = Id.str();
      break;
    }
    case 'O': {
      uint64_t OO;
      if (!num(OO))
        return false;
      if (OO == OO_None || OO >= NUM_OVERLOADED_OPERATORS)
        return malformed();
      Name = C.DeclarationNames.getCXXOperatorName(
          static_cast<OverloadedOperatorKind>(OO));
      Spelling = Name.getAsString();
      break;
    }
    case 'L': {
      StringRef Id;
      if (!str(Id))
        return false;
      Name = C.DeclarationNames.getCXXLiteralOperatorName(&C.Idents.get(Id));
      Spelling = Name.getAsString();
      break;
    }
    case 'C': {
      QualType To;
      if (!type(To))
        return false;
      Name = C.DeclarationNames.getCXXConversionFunctionName(
          C.getCanonicalType(To));
      Spelling = Name.getAsString();
      break;
    }
    case 'c':
    case 'd': {
      bool IsCtor = S[Pos - 1] == 'c';
      if (ClassTy.isNull())
        return malformed();
      CanQualType CT = C.getCanonicalType(ClassTy);
      Name = IsCtor ? C.DeclarationNames.getCXXConstructorName(CT)
                    : C.DeclarationNames.getCXXDestructorName(CT);
      Spelling = Name.getAsString();
      break;
    }
    default:
      return malformed();
    }
    QualType FnTy;
    if (Kind == 'f' && !type(FnTy))
      return false;

    std::string QualName = qualify(Sc, Spelling);
    auto Lookup = Sc.DC ? Sc.DC->lookup(Name) : DeclContextLookupResult();
    TemplateDecl *Template = nullptr;
    for (NamedDecl *D : Lookup) {
      // (Through a using-declaration, but not through a namespace alias: its
      // reflection is of the alias.)
      if (auto *USD = dyn_cast<UsingShadowDecl>(D))
        D = USD->getTargetDecl();
      switch (Kind) {
      case 'n':
        if (isa<NamespaceDecl, NamespaceAliasDecl>(D)) {
          RV = APValue(ReflectionKind::Namespace, D->getCanonicalDecl());
          return true;
        }
        break;
      case 't':
        if (auto *TD = dyn_cast<TypeDecl>(D)) {
          RV = APValue(ReflectionKind::Type,
                       C.getTypeDeclType(TD).getAsOpaquePtr());
          return true;
        }
        break;
      case 'T':
        if (auto *TD = dyn_cast<TemplateDecl>(D)) {
          if (Template &&
              Template->getCanonicalDecl() != TD->getCanonicalDecl())
            return H.fail("the plugin refers to the template '" + QualName +
                          "', and there is more than one template of that "
                          "name in this translation unit");
          Template = TD;
        }
        break;
      case 'v':
        if (isa<VarDecl, FieldDecl, EnumConstantDecl>(D)) {
          RV = APValue(ReflectionKind::Declaration,
                       cast<ValueDecl>(D->getCanonicalDecl()));
          return true;
        }
        break;
      case 'f':
        if (auto *FD = dyn_cast<FunctionDecl>(D);
            FD && C.hasSameType(FD->getType(), FnTy)) {
          RV = APValue(ReflectionKind::Declaration,
                       cast<ValueDecl>(FD->getCanonicalDecl()));
          return true;
        }
        break;
      default:
        return malformed();
      }
    }
    if (Template) {
      if (auto *RTD = dyn_cast<RedeclarableTemplateDecl>(Template))
        Template = RTD->getCanonicalDecl();
      RV = APValue(ReflectionKind::Template,
                   TemplateName(Template).getAsVoidPointer());
      return true;
    }
    if (fallback(Kind, QualName, RV))
      return true;
    if (Kind == 'f' && !Lookup.empty())
      return H.fail("the plugin refers to the function '" + QualName +
                    "' of type '" + FnTy.getAsString(C.getPrintingPolicy()) +
                    "', which is not declared in this translation unit");
    return notDeclared(QualName);
  }

  bool specialization(APValue &RV) {
    APValue TemplRV;
    if (!reflection(TemplRV))
      return false;
    if (!TemplRV.isReflectedTemplate())
      return malformed();
    TemplateDecl *TD = TemplRV.getReflectedTemplate().getAsTemplateDecl();
    uint64_t N;
    if (!num(N))
      return false;
    SmallVector<TemplateArgument, 8> Args;
    for (uint64_t I = 0; I != N; ++I) {
      switch (next()) {
      case 't': {
        QualType T;
        if (!type(T))
          return false;
        Args.push_back(TemplateArgument(T));
        break;
      }
      case 'i': {
        QualType T;
        if (!type(T))
          return false;
        size_t End = S.find(';', Pos);
        if (End == StringRef::npos || !T->isIntegralOrEnumerationType() ||
            T->isIncompleteType())
          return malformed();
        llvm::APSInt V(C.getIntWidth(T),
                       T->isUnsignedIntegerOrEnumerationType());
        llvm::APInt Parsed;
        StringRef Digits = S.substr(Pos, End - Pos);
        bool Negative = Digits.consume_front("-");
        if (Digits.getAsInteger(10, Parsed))
          return malformed();
        Parsed = Parsed.zextOrTrunc(V.getBitWidth());
        V = Negative ? -Parsed : Parsed;
        Pos = End + 1;
        Args.push_back(TemplateArgument(C, V, T));
        break;
      }
      case 'T': {
        APValue A;
        if (!reflection(A))
          return false;
        if (!A.isReflectedTemplate())
          return malformed();
        Args.push_back(TemplateArgument(A.getReflectedTemplate()));
        break;
      }
      case 'd': {
        APValue A;
        if (!reflection(A))
          return false;
        if (!A.isReflectedDecl())
          return malformed();
        QualType T;
        if (!type(T))
          return false;
        Args.push_back(TemplateArgument(A.getReflectedDecl(), T));
        break;
      }
      case 'n': {
        QualType T;
        if (!type(T))
          return false;
        Args.push_back(TemplateArgument(T, /*isNullPtr=*/true));
        break;
      }
      default:
        return malformed();
      }
    }
    if (SubstituteReflectedTemplate(RV, C, H.Meta, hostEvaluator(H),
                                    hostDiagnoser(H), TD, Args, H.range(),
                                    H.CallLoc, /*NoDiagnose=*/false))
      return H.fail("the plugin refers to a specialization of '" +
                    TD->getQualifiedNameAsString() +
                    "' that cannot be formed in this translation unit");
    return true;
  }

public:
  ReflectionDecoder(ActiveHostState &H, StringRef S) : H(H), C(H.Ctx), S(S) {}

  bool done() { return atEnd() || malformed(); }

  bool type(QualType &T) {
    switch (peek()) {
    case 'N':
    case 'S': {
      APValue RV;
      if (!reflection(RV))
        return false;
      if (!RV.isReflectedType())
        return malformed();
      T = RV.getReflectedType();
      return true;
    }
    default:
      break;
    }
    uint64_t N;
    switch (next()) {
    case 'b':
      return num(N) && builtin(N, T);
    case 'Q':
      if (!num(N) || !type(T))
        return false;
      T = C.getQualifiedType(T, Qualifiers::fromCVRMask(N));
      return true;
    case 'P':
      if (!type(T))
        return false;
      T = C.getPointerType(T);
      return true;
    case 'L':
      if (!type(T))
        return false;
      T = C.getLValueReferenceType(T);
      return true;
    case 'R':
      if (!type(T))
        return false;
      T = C.getRValueReferenceType(T);
      return true;
    case 'A':
      if (!num(N) || !type(T))
        return false;
      T = C.getConstantArrayType(T,
                                 llvm::APInt(C.getTypeSize(C.getSizeType()), N),
                                 nullptr, ArraySizeModifier::Normal, 0);
      return true;
    case 'U':
      if (!type(T))
        return false;
      T = C.getIncompleteArrayType(T, ArraySizeModifier::Normal, 0);
      return true;
    case 'F': {
      uint64_t Quals, Ref, Flags, NumParams;
      QualType Ret;
      if (!num(Quals) || !num(Ref) || !num(Flags) || !type(Ret) ||
          !num(NumParams))
        return false;
      SmallVector<QualType, 8> Params;
      for (uint64_t I = 0; I != NumParams; ++I) {
        QualType P;
        if (!type(P))
          return false;
        Params.push_back(P);
      }
      FunctionProtoType::ExtProtoInfo EPI;
      EPI.TypeQuals = Qualifiers::fromCVRMask(Quals);
      EPI.RefQualifier = static_cast<RefQualifierKind>(Ref);
      EPI.Variadic = Flags & 1;
      if (Flags & 2)
        EPI.ExceptionSpec.Type = EST_BasicNoexcept;
      T = C.getFunctionType(Ret, Params, EPI);
      return true;
    }
    case 'M': {
      QualType Class, Pointee;
      if (!type(Class) || !type(Pointee))
        return false;
      const CXXRecordDecl *RD = Class->getAsCXXRecordDecl();
      if (!RD)
        return malformed();
      T = C.getMemberPointerType(Pointee, std::nullopt, RD);
      return true;
    }
    default:
      return malformed();
    }
  }

  bool reflection(APValue &RV) {
    switch (peek()) {
    case '0':
      ++Pos;
      RV = APValue(ReflectionKind::Null, nullptr);
      return true;
    case 'G':
      ++Pos;
      RV = APValue(ReflectionKind::Namespace, C.getTranslationUnitDecl());
      return true;
    case 'N':
      ++Pos;
      return named(RV);
    case 'S':
      ++Pos;
      return specialization(RV);
    default: {
      QualType T;
      if (!type(T))
        return false;
      RV = APValue(ReflectionKind::Type, T.getAsOpaquePtr());
      return true;
    }
    }
  }
};

//===----------------------------------------------------------------------===//
// The host interface
//===----------------------------------------------------------------------===//

bool callMetafn(ActiveHostState &H, unsigned MetaFnID, const ConstevalArg *Args,
                size_t NumArgs, ConstevalArg *Out) {
  const Metafunction *Metafn = nullptr;
  if (Metafunction::Lookup(MetaFnID, Metafn) || !Metafn)
    return H.fail("the plugin called a metafunction this compiler does not "
                  "have");

  ASTContext &C = H.Ctx;
  SourceLocation Loc = H.CallLoc;
  SourceRange Range = H.range();
  Metafunction::EvaluateFn Evaluator = hostEvaluator(H);
  Metafunction::DiagnoseFn Diagnoser = hostDiagnoser(H);

  auto Reflection = [&](uint64_t Handle, Expr *&E) {
    APValue RV;
    if (!H.lookupReflection(Handle, RV))
      return false;
    E = makeConstArgExpr(C, Loc, RV, C.MetaInfoTy);
    return true;
  };
  auto SizeT = [&](uint64_t V) {
    return makeConstArgExpr(C, Loc, APValue(C.MakeIntValue(V, C.getSizeType())),
                            C.getSizeType());
  };

  // Special case 1: members_gather(info* dst, r, kind, scope, designating, cap)
  if (Metafn->isMemberGather()) {
    if (NumArgs < 6)
      return H.fail("the plugin called a metafunction with too few arguments");
    SmallVector<Expr *, 6> ArgExprs(6);
    ArgExprs[0] = makeConstArgExpr(C, Loc, APValue(), C.VoidPtrTy);
    ArgExprs[2] = SizeT(Args[2].Val);
    ArgExprs[5] = SizeT(Args[5].Val);
    if (!Reflection(Args[1].Val, ArgExprs[1]) ||
        !Reflection(Args[3].Val, ArgExprs[3]) ||
        !Reflection(Args[4].Val, ArgExprs[4]))
      return false;

    APValue Gathered;
    if (Metafn->evaluate(Gathered, C, H.Meta, Evaluator, Diagnoser,
                         H.AllowInjection, C.getSizeType(), Range, ArgExprs,
                         H.ContainingDecl))
      return H.fail();
    unsigned N = Gathered.getArraySize();
    auto *Dst = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(Args[0].Val));
    if (Dst) {
      if (N != Args[5].Val)
        return H.fail("the members of a class changed while the plugin was "
                      "reading them");
      size_t Stride = C.getTypeSizeInChars(C.MetaInfoTy).getQuantity();
      for (unsigned I = 0; I != N; ++I) {
        uint8_t *Slot = Dst + I * Stride;
        std::memset(Slot, 0, Stride);
        uint64_t Handle =
            H.internReflection(Gathered.getArrayInitializedElt(I));
        std::memcpy(Slot, &Handle, std::min(Stride, sizeof(Handle)));
      }
    }
    Out->Kind = CEK_Int;
    Out->Val = N;
    return true;
  }

  // Special case 2: tokens_join(const token* ptr, size_t n)
  if (Metafn->getTokenArrayOp() == Metafunction::TokenArrayOp::Join) {
    if (NumArgs < 2)
      return H.fail("the plugin called a metafunction with too few arguments");
    auto *Ptr =
        reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(Args[0].Val));
    size_t N = static_cast<size_t>(Args[1].Val);
    size_t Stride = C.getTypeSizeInChars(C.MetaTokenTy).getQuantity();
    SmallVector<Token, 32> Toks;
    for (size_t I = 0; I < N; ++I) {
      uint64_t Handle = 0;
      std::memcpy(&Handle, Ptr + I * Stride, std::min(Stride, sizeof(Handle)));
      TokenSequenceData TSD;
      if (!H.lookupTokenSeq(Handle, TSD))
        return false;
      Toks.append(TSD.begin(), TSD.end());
    }
    TokenSequenceData Joined = CreateTokenSequenceData(C, Toks);
    Out->Kind = CEK_TokenSequence;
    Out->Val = H.internTokenSeq(Joined);
    return true;
  }
  if (Metafn->getTokenArrayOp() != Metafunction::TokenArrayOp::None)
    return H.fail("the plugin called a metafunction that cannot be called "
                  "from a plugin yet");

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
          Args[0].Kind == CEK_MetaInfo) {
        APValue TyRefl;
        if (!H.lookupReflection(Args[0].Val, TyRefl))
          return false;
        if (TyRefl.isReflectedType() &&
            TyRefl.getReflectedType()->isIntegralOrEnumerationType())
          ITy = TyRefl.getReflectedType();
      }
      ArgExprs.push_back(
          makeConstArgExpr(C, Loc, APValue(C.MakeIntValue(A.Val, ITy)), ITy));
      break;
    }
    case CEK_MetaInfo: {
      Expr *E;
      if (!Reflection(A.Val, E))
        return false;
      ArgExprs.push_back(E);
      break;
    }
    case CEK_TokenSequence:
    case CEK_MetaToken: {
      TokenSequenceData TSD;
      if (!H.lookupTokenSeq(A.Val, TSD))
        return false;
      ArgExprs.push_back(
          makeConstArgExpr(C, Loc, APValue(TSD), C.TokenSequenceTy));
      break;
    }
    case CEK_CharPtr: {
      APValue LV;
      if (!H.importChars(
              reinterpret_cast<const char *>(static_cast<uintptr_t>(A.Val)),
              A.Len ? std::optional<size_t>(A.Len) : std::nullopt,
              C.CharTy.withConst(), LV))
        return false;
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
      return H.fail("the plugin called a metafunction with an argument of a "
                    "type that cannot be passed from a plugin yet");
    }
  }

  QualType ResTy;
  switch (Metafn->getResultKind()) {
  case Metafunction::MFRK_spliceFromArg: {
    if (NumArgs < 1 || Args[0].Kind != CEK_MetaInfo)
      return H.fail("the plugin called a metafunction with too few arguments");
    APValue TyRefl;
    if (!H.lookupReflection(Args[0].Val, TyRefl))
      return false;
    if (!TyRefl.isReflectedType())
      return H.fail("the plugin called a metafunction whose result type is "
                    "not a type");
    ResTy = TyRefl.getReflectedType();
    break;
  }
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
    return H.fail("the plugin called a metafunction whose result cannot be "
                  "returned to a plugin yet");
  }

  APValue ResVal;
  if (Metafn->evaluate(ResVal, C, H.Meta, Evaluator, Diagnoser,
                       H.AllowInjection, ResTy, Range, ArgExprs,
                       H.ContainingDecl))
    return H.fail();

  // Convert APValue result back to ConstevalArg.
  Out->Aux = 0;
  Out->Len = 0;
  if (ResVal.isAbsent() || ResTy->isVoidType()) {
    Out->Kind = CEK_Void;
    Out->Val = 0;
  } else if (ResVal.isInt() && ResVal.getInt().getSignificantBits() <= 64) {
    Out->Kind = ResTy->isBooleanType() ? CEK_Bool : CEK_Int;
    Out->Val = ResVal.getInt().isSigned()
                   ? static_cast<uint64_t>(ResVal.getInt().getSExtValue())
                   : ResVal.getInt().getZExtValue();
  } else if (ResVal.isReflection()) {
    Out->Kind = CEK_MetaInfo;
    Out->Val = H.internReflection(ResVal);
  } else if (ResVal.isTokenSequence()) {
    Out->Kind = CEK_TokenSequence;
    Out->Val = H.internTokenSeq(ResVal.getTokenSequence());
  } else if (ResVal.isLValue() && ResTy->isPointerType() &&
             isPluginCharType(ResTy->getPointeeType())) {
    Out->Kind = CEK_CharPtr;
    Out->Val = 0;
    if (!ResVal.isNullPointer()) {
      const char *P =
          H.exportChars(ResVal, ResTy->getPointeeType().getUnqualifiedType());
      if (!P)
        return H.fail("a metafunction returned a pointer to characters that "
                      "cannot be read to give them to the plugin");
      Out->Val = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(P));
    }
  } else if (ResVal.isLValue() &&
             Metafn->getResultKind() == Metafunction::MFRK_tokenPtr) {
    // Materialize a native array of token handles in ScratchAlloc.
    Out->Kind = CEK_TokenPtr;
    Out->Val = 0;
    if (!ResVal.isNullPointer()) {
      const ValueDecl *VD =
          ResVal.getLValueBase().dyn_cast<const ValueDecl *>();
      const TokenSequenceData *TSD = getMaterializedTokens(VD);
      if (!TSD)
        return H.fail("a metafunction returned a pointer to tokens that "
                      "cannot be given to the plugin");
      size_t Stride = C.getTypeSizeInChars(C.MetaTokenTy).getQuantity();
      uint8_t *Arr = H.ScratchAlloc.Allocate<uint8_t>(TSD->size() * Stride);
      std::memset(Arr, 0, TSD->size() * Stride);
      for (size_t I = 0; I < TSD->size(); ++I) {
        uint64_t Handle =
            H.internTokenSeq(TokenSequenceData(TSD->data() + I, 1));
        std::memcpy(Arr + I * Stride, &Handle,
                    std::min(Stride, sizeof(Handle)));
      }
      Out->Val = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Arr));
      Out->Len = TSD->size();
    }
  } else {
    return H.fail("a metafunction returned a value of type '" +
                  ResTy.getAsString(C.getPrintingPolicy()) +
                  "', which cannot be returned to a plugin yet");
  }
  return true;
}

bool makeTokenSeq(ActiveHostState &H, const ConstevalTokenDesc *Toks,
                  size_t NumToks, const ConstevalArg *Splices,
                  size_t NumSplices, uint64_t &Result) {
  ASTContext &C = H.Ctx;
  SourceLocation Loc = H.CallLoc;
  SmallVector<Token, 32> Out;
  Out.reserve(NumToks);
  const unsigned KeptFlags = Token::LeadingSpace | Token::StartOfLine;

  for (size_t I = 0; I < NumToks; ++I) {
    const ConstevalTokenDesc &D = Toks[I];
    tok::TokenKind K = static_cast<tok::TokenKind>(D.TokKind);
    Token T;
    T.startToken();
    T.setKind(K);
    T.setLocation(Loc);
    T.setFlag(static_cast<Token::TokenFlags>(D.Flags & KeptFlags));

    if (K == tok::annot_token_seq_expr) {
      if (D.SpliceIdx >= NumSplices)
        return H.fail("malformed token sequence in the plugin");
      T.setAnnotationEndLoc(Loc);
      const ConstevalArg &S = Splices[D.SpliceIdx];
      QualType Ty;
      APValue Val;
      MetaTokenEnumKind EnumKind = MetaTokenEnumKind::None;
      switch (S.Kind) {
      case CEK_TokenSequence:
      case CEK_MetaToken: {
        TokenSequenceData TSD;
        if (!H.lookupTokenSeq(S.Val, TSD))
          return false;
        Ty = C.TokenSequenceTy;
        Val = APValue(TSD);
        break;
      }
      case CEK_MetaInfo:
        Ty = C.MetaInfoTy;
        if (!H.lookupReflection(S.Val, Val))
          return false;
        break;
      case CEK_Bool:
      case CEK_Int: {
        EnumKind = static_cast<MetaTokenEnumKind>(S.Aux);
        if (EnumKind != MetaTokenEnumKind::None) {
          Ty = C.getSizeType();
        } else {
          // The plugin says what type the value has.
          const char *Desc =
              reinterpret_cast<const char *>(static_cast<uintptr_t>(S.Len));
          if (!Desc)
            return H.fail("malformed token sequence in the plugin");
          ReflectionDecoder Decoder(H, Desc);
          if (!Decoder.type(Ty) || !Decoder.done())
            return false;
          if (!Ty->isIntegralOrEnumerationType() || Ty->isIncompleteType())
            return H.fail("malformed token sequence in the plugin");
        }
        llvm::APSInt V(llvm::APInt(C.getIntWidth(Ty), S.Val,
                                   /*isSigned=*/false, /*implicitTrunc=*/true),
                       Ty->isUnsignedIntegerOrEnumerationType());
        Val = APValue(V);
        break;
      }
      default:
        return H.fail("the plugin interpolates a value of a type that cannot "
                      "be interpolated from a plugin yet");
      }
      unsigned DiagID = 0;
      std::string DiagArg;
      if (!InterpolateValueTokens(C, T, Ty, EnumKind, Val, Loc,
                                  /*SourceExpr=*/nullptr, Out, DiagID,
                                  DiagArg)) {
        H.note(DiagID) << DiagArg;
        return H.fail();
      }
      continue;
    }

    if (D.Text && D.TextLen > 0) {
      llvm::StringRef Text(D.Text, D.TextLen);
      if (K == tok::identifier || tok::getKeywordSpelling(K) != nullptr) {
        T.setIdentifierInfo(&C.Idents.get(Text));
        T.setLength(D.TextLen);
      } else {
        const char *Buf = H.copyString(Text);
        T.setLiteralData(Buf);
        T.setLength(D.TextLen);
      }
    } else if (const char *Spell = tok::getPunctuatorSpelling(K)) {
      T.setLength(std::strlen(Spell));
    } else if (const char *Spell = tok::getKeywordSpelling(K)) {
      T.setIdentifierInfo(&C.Idents.get(Spell));
      T.setLength(std::strlen(Spell));
    } else {
      return H.fail("malformed token sequence in the plugin");
    }
    Out.push_back(T);
  }

  Result = H.internTokenSeq(CreateTokenSequenceData(C, Out));
  return true;
}

bool reflect(ActiveHostState &H, const char *Desc, size_t Len,
             uint64_t &Result) {
  auto [It, New] = H.Described.try_emplace(Desc, 0);
  if (!New) {
    Result = It->second;
    return true;
  }
  ReflectionDecoder Decoder(H, StringRef(Desc, Len));
  APValue RV;
  if (!Decoder.reflection(RV) || !Decoder.done())
    return false;
  Result = H.internReflection(RV);
  // (Decoding may have described other reflections on the way.)
  H.Described[Desc] = Result;
  return true;
}

bool metaEqual(ActiveHostState &H, uint64_t LHS, uint64_t RHS, bool &Result) {
  APValue L, R;
  if (!H.lookupReflection(LHS, L) || !H.lookupReflection(RHS, R))
    return false;
  // As == in the constant evaluator.
  llvm::FoldingSetNodeID LID, RID;
  L.Profile(LID);
  R.Profile(RID);
  Result = LID == RID;
  return true;
}

/// Returns false if the invocation is to be abandoned.
bool reportDiag(ActiveHostState &H, uint32_t Kind, StringRef Tag,
                StringRef Msg) {
  DiagnosticsEngine &Diags = H.Ctx.getDiagnostics();
  switch (Kind) {
  case 0:
    Diags.setLastDiagnosticIgnored(false);
    if (Tag.empty())
      Diags.Report(H.CallLoc, diag::note_constexpr_message) << Msg;
    else
      Diags.Report(H.CallLoc, diag::note_constexpr_message_tag) << Tag << Msg;
    return true;
  case 1:
    if (Tag.empty())
      Diags.Report(H.CallLoc, diag::warn_constexpr_message) << Msg;
    else
      Diags.Report(H.CallLoc, diag::warn_constexpr_message_tag) << Tag << Msg;
    return true;
  default:
    H.Failed = true;
    H.Failure.IsUserError = true;
    H.Failure.Tag = Tag.str();
    H.Failure.Message = Msg.str();
    return false;
  }
}

bool queueInjection(ActiveHostState &H, uint64_t TargetNSHandle,
                    uint64_t TokenSeqHandle) {
  if (!H.AllowInjection || !H.EvalStatus) {
    H.note(diag::metafn_injected_decl_non_plainly_consteval);
    return H.fail();
  }
  DeclContext *TargetDC = nullptr;
  if (TargetNSHandle != 0) {
    APValue NS;
    if (!H.lookupReflection(TargetNSHandle, NS))
      return false;
    if (!NS.isReflectedNamespace()) {
      H.note(diag::metafn_builtin_inject_target_not_namespace);
      return H.fail();
    }
    TargetDC = dyn_cast<DeclContext>(NS.getReflectedNamespace());
  }
  TokenSequenceData TSD;
  if (!H.lookupTokenSeq(TokenSeqHandle, TSD))
    return false;
  H.EvalStatus->PendingInjections.push_back({H.CallLoc, TargetDC, TSD});
  return true;
}

bool makeIdOrStrLit(ActiveHostState &H, bool IsStrLit,
                    const ConstevalArg *Parts, size_t NumParts,
                    uint64_t &Result) {
  ASTContext &C = H.Ctx;
  SmallString<64> Buf;
  for (size_t I = 0; I < NumParts; ++I) {
    const ConstevalArg &P = Parts[I];
    switch (P.Kind) {
    case CEK_CharPtr: {
      const char *S =
          reinterpret_cast<const char *>(static_cast<uintptr_t>(P.Val));
      if (!S && P.Len)
        return H.fail("the plugin passed a null pointer as a string");
      if (S)
        Buf.append(StringRef(S, P.Aux ? P.Len : std::strlen(S)));
      break;
    }
    case CEK_Char:
      Buf.push_back(static_cast<char>(P.Val));
      break;
    case CEK_Int:
    case CEK_Bool:
      // (Aux: whether the value is unsigned.)
      if (P.Aux)
        Buf.append(std::to_string(P.Val));
      else
        Buf.append(std::to_string(static_cast<int64_t>(P.Val)));
      break;
    default:
      return H.fail("the plugin names something with a value of a type that "
                    "cannot be used for that from a plugin yet");
    }
  }

  TokenSequenceData TSD;
  if (IsStrLit) {
    TSD = MakeStringLiteralTokenSequence(C, Buf, H.CallLoc);
  } else {
    bool LexesAsIdentifier = false;
    if (!MakeIdentifierTokenSequence(C, Buf, H.CallLoc, TSD,
                                     LexesAsIdentifier)) {
      H.note(diag::note_constexpr_id_not_identifier)
          << Buf << LexesAsIdentifier;
      return H.fail();
    }
  }
  Result = H.internTokenSeq(TSD);
  return true;
}

// The entry points. Each does its work in a function of its own, so that
// everything that work constructed is gone before an invocation is abandoned.

bool hostCallMetafn(unsigned MetaFnID, unsigned /*ResultKind*/,
                    const ConstevalArg *Args, size_t NumArgs,
                    ConstevalArg *Out) {
  ActiveHostState &H = currentHost();
  if (!callMetafn(H, MetaFnID, Args, NumArgs, Out))
    abandon(H);
  return true;
}

uint64_t hostMakeTokenSeq(const ConstevalTokenDesc *Toks, size_t NumToks,
                          const ConstevalArg *Splices, size_t NumSplices) {
  ActiveHostState &H = currentHost();
  uint64_t Result = 0;
  if (!makeTokenSeq(H, Toks, NumToks, Splices, NumSplices, Result))
    abandon(H);
  return Result;
}

uint64_t hostReflect(const char *Desc, size_t Len) {
  ActiveHostState &H = currentHost();
  uint64_t Result = 0;
  if (!reflect(H, Desc, Len, Result))
    abandon(H);
  return Result;
}

void hostUnhandledException() {
  ActiveHostState &H = currentHost();
  H.fail("an exception was thrown and not caught");
  abandon(H);
}

bool hostMetaEqual(uint64_t LHS, uint64_t RHS) {
  ActiveHostState &H = currentHost();
  bool Result = false;
  if (!metaEqual(H, LHS, RHS, Result))
    abandon(H);
  return Result;
}

void hostReportDiag(uint32_t Kind, const char *Tag, size_t TagLen,
                    const char *Msg, size_t MsgLen) {
  ActiveHostState &H = currentHost();
  if (!reportDiag(H, Kind, StringRef(Tag ? Tag : "", Tag ? TagLen : 0),
                  StringRef(Msg ? Msg : "", Msg ? MsgLen : 0)))
    abandon(H);
}

void hostQueueInjection(uint64_t TargetNSHandle, uint64_t TokenSeqHandle) {
  ActiveHostState &H = currentHost();
  if (!queueInjection(H, TargetNSHandle, TokenSeqHandle))
    abandon(H);
}

uint64_t hostMakeIdOrStrLit(bool IsStrLit, const ConstevalArg *Parts,
                            size_t NumParts) {
  ActiveHostState &H = currentHost();
  uint64_t Result = 0;
  if (!makeIdOrStrLit(H, IsStrLit, Parts, NumParts, Result))
    abandon(H);
  return Result;
}

bool stringizeTokens(ActiveHostState &H, uint64_t TokenSeqHandle,
                     const char *&Result) {
  TokenSequenceData TSD;
  if (!H.lookupTokenSeq(TokenSeqHandle, TSD))
    return false;
  Result = H.copyString(StringizeTokenSequence(H.Ctx, TSD));
  return true;
}

const char *hostStringizeTokens(uint64_t TokenSeqHandle) {
  ActiveHostState &H = currentHost();
  const char *Result = nullptr;
  if (!stringizeTokens(H, TokenSeqHandle, Result))
    abandon(H);
  return Result;
}

void hostUnsupported(const char *Msg, size_t Len) {
  ActiveHostState &H = currentHost();
  H.fail(StringRef(Msg, Len));
  abandon(H);
}

const ConstevalHostAPI GlobalHostAPI = {
    /*Version=*/2,
    /*Reserved=*/0,          &hostCallMetafn,
    &hostMakeTokenSeq,       &hostReflect,
    &hostUnhandledException, &hostMetaEqual,
    &hostReportDiag,         &hostQueueInjection,
    &hostMakeIdOrStrLit,     &hostStringizeTokens,
    &hostUnsupported,
};

//===----------------------------------------------------------------------===//
// Values, to and from the plugin's representation
//===----------------------------------------------------------------------===//

class Marshaller {
  ActiveHostState &H;
  ASTContext &C;

  bool unsupported(QualType T, StringRef Why) {
    Reason = ("'" + T.getAsString(C.getPrintingPolicy()) + "' " + Why).str();
    return false;
  }

  /// Whether a class type is one whose value is that of its bases and
  /// members, each at its offset.
  bool checkRecord(QualType T, const RecordDecl *RD) {
    if (RD->isInvalidDecl() || !RD->isCompleteDefinition())
      return unsupported(T, "is incomplete");
    if (RD->isUnion())
      return unsupported(T, "is a union");
    if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD);
        CXXRD && (CXXRD->isPolymorphic() || CXXRD->getNumVBases()))
      return unsupported(T, "has virtual functions or virtual base classes");
    return true;
  }

public:
  /// Why a value could not cross, when one could not.
  std::string Reason;

  Marshaller(ActiveHostState &H) : H(H), C(H.Ctx) {}

  size_t sizeOf(QualType T) { return pluginSizeOf(C, T); }

  /// Write \p V, of type \p Ty, as the plugin represents it, to the zeroed
  /// \p Buf.
  bool write(QualType Ty, const APValue &V, uint8_t *Buf) {
    QualType T = Ty.getCanonicalType().getUnqualifiedType();
    auto Store = [&](uint64_t Bits, size_t Size) {
      std::memcpy(Buf, &Bits, std::min(Size, sizeof(Bits)));
      return true;
    };
    if (!V.hasValue())
      return unsupported(Ty, "has no value here");
    if (T->isIncompleteType())
      return unsupported(Ty, "is incomplete");

    if (T->isReflectionType()) {
      if (!V.isReflection())
        return unsupported(Ty, "has a value that is not a reflection");
      return Store(H.internReflection(V), 8);
    }
    if (T->isTokenSequenceType() || T->isMetaTokenType()) {
      if (!V.isTokenSequence())
        return unsupported(Ty, "has a value that is not a token sequence");
      return Store(H.internTokenSeq(V.getTokenSequence()), 8);
    }
    if (T->isStdMetaTokenSequenceType()) {
      // (The object, or -- for a macro's parameter -- its tokens.)
      if (V.isTokenSequence())
        return Store(H.internTokenSeq(V.getTokenSequence()), 8);
      if (V.isStruct() && V.getStructNumFields() == 1 &&
          V.getStructField(0).isTokenSequence())
        return Store(H.internTokenSeq(V.getStructField(0).getTokenSequence()),
                     8);
      if (V.isStruct() && V.getStructNumFields() == 0)
        return true;
      return unsupported(Ty, "has a value that is not a token sequence");
    }
    if (T->isNullPtrType())
      return true;
    if (T->isIntegralOrEnumerationType()) {
      size_t Size = sizeOf(T);
      if (!V.isInt() || Size > 8)
        return unsupported(Ty, "is not an integer of at most 64 bits");
      return Store(V.getInt().getZExtValue(), Size);
    }
    if (T->isRealFloatingType()) {
      const llvm::fltSemantics &Sem = C.getFloatTypeSemantics(T);
      if (!V.isFloat() || (&Sem != &llvm::APFloat::IEEEsingle() &&
                           &Sem != &llvm::APFloat::IEEEdouble()))
        return unsupported(Ty, "is not float or double");
      return Store(V.getFloat().bitcastToAPInt().getZExtValue(), sizeOf(T));
    }
    if (T->isPointerType()) {
      QualType Pointee = T->getPointeeType();
      if (!isPluginCharType(Pointee))
        return unsupported(Ty, "is a pointer to something other than "
                               "characters");
      if (!Pointee.isConstQualified())
        return unsupported(Ty, "points to characters the plugin could "
                               "change, and the change would be lost");
      if (!V.isLValue())
        return unsupported(Ty, "has a value that is not a pointer");
      if (V.isNullPointer())
        return true;
      const char *P = H.exportChars(V, Pointee.getUnqualifiedType());
      if (!P)
        return unsupported(Ty, "points to characters that cannot be read");
      return Store(reinterpret_cast<uintptr_t>(P), sizeof(P));
    }
    if (const ConstantArrayType *CAT = C.getAsConstantArrayType(T)) {
      if (!V.isArray())
        return unsupported(Ty, "has a value that is not an array");
      QualType EltTy = CAT->getElementType();
      size_t EltSize = sizeOf(EltTy);
      uint64_t N = CAT->getZExtSize();
      for (uint64_t I = 0; I != N; ++I) {
        const APValue &Elt = I < V.getArrayInitializedElts()
                                 ? V.getArrayInitializedElt(I)
                                 : V.getArrayFiller();
        if (!write(EltTy, Elt, Buf + I * EltSize))
          return false;
      }
      return true;
    }
    if (const RecordDecl *RD = T->getAsRecordDecl()) {
      if (!checkRecord(Ty, RD))
        return false;
      if (!V.isStruct())
        return unsupported(Ty, "has a value that is not a class object");
      const ASTRecordLayout &Layout = C.getASTRecordLayout(RD);
      if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
        unsigned I = 0;
        for (const CXXBaseSpecifier &B : CXXRD->bases()) {
          const CXXRecordDecl *BD = B.getType()->getAsCXXRecordDecl();
          if (!write(B.getType(), V.getStructBase(I++),
                     Buf + Layout.getBaseClassOffset(BD).getQuantity()))
            return false;
        }
      }
      for (const FieldDecl *FD : RD->fields()) {
        if (FD->isUnnamedBitField())
          continue;
        if (FD->isBitField())
          return unsupported(Ty, "has a bit-field");
        if (!write(FD->getType(), V.getStructField(FD->getFieldIndex()),
                   Buf + Layout.getFieldOffset(FD->getFieldIndex()) / 8))
          return false;
      }
      return true;
    }
    return unsupported(Ty, "is not a type whose values can be passed yet");
  }

  /// Read the value of type \p Ty the plugin left at \p Buf.
  bool read(QualType Ty, const uint8_t *Buf, APValue &Result) {
    QualType T = Ty.getCanonicalType().getUnqualifiedType();
    auto Load = [&](size_t Size) {
      uint64_t Bits = 0;
      std::memcpy(&Bits, Buf, std::min(Size, sizeof(Bits)));
      return Bits;
    };

    if (T->isVoidType()) {
      Result = APValue();
      return true;
    }
    if (T->isIncompleteType())
      return unsupported(Ty, "is incomplete");
    if (T->isReflectionType())
      return H.lookupReflection(Load(8), Result);
    if (T->isTokenSequenceType() || T->isMetaTokenType() ||
        T->isStdMetaTokenSequenceType()) {
      TokenSequenceData TSD;
      if (!H.lookupTokenSeq(Load(8), TSD))
        return false;
      if (T->isStdMetaTokenSequenceType()) {
        Result = APValue(APValue::UninitStruct(), 0, 1);
        Result.getStructField(0) = APValue(TSD);
      } else {
        Result = APValue(TSD);
      }
      return true;
    }
    if (T->isNullPtrType()) {
      Result = APValue(APValue::LValueBase(), CharUnits::Zero(),
                       APValue::NoLValuePath(), /*IsNullPtr=*/true);
      return true;
    }
    if (T->isIntegralOrEnumerationType()) {
      size_t Size = sizeOf(T);
      if (Size > 8)
        return unsupported(Ty, "is not an integer of at most 64 bits");
      uint64_t Bits = Load(Size);
      if (T->isBooleanType())
        Bits = Bits != 0;
      Result = APValue(
          llvm::APSInt(llvm::APInt(C.getIntWidth(T), Bits, /*isSigned=*/false,
                                   /*implicitTrunc=*/true),
                       T->isUnsignedIntegerOrEnumerationType()));
      return true;
    }
    if (T->isRealFloatingType()) {
      const llvm::fltSemantics &Sem = C.getFloatTypeSemantics(T);
      if (&Sem != &llvm::APFloat::IEEEsingle() &&
          &Sem != &llvm::APFloat::IEEEdouble())
        return unsupported(Ty, "is not float or double");
      size_t Size = sizeOf(T);
      Result = APValue(llvm::APFloat(Sem, llvm::APInt(Size * 8, Load(Size))));
      return true;
    }
    if (T->isPointerType()) {
      QualType Pointee = T->getPointeeType();
      if (!isPluginCharType(Pointee))
        return unsupported(Ty, "is a pointer to something other than "
                               "characters");
      const char *P = nullptr;
      std::memcpy(&P, Buf, sizeof(P));
      return H.importChars(P, std::nullopt, Pointee, Result);
    }
    if (const ConstantArrayType *CAT = C.getAsConstantArrayType(T)) {
      uint64_t N = CAT->getZExtSize();
      QualType EltTy = CAT->getElementType();
      size_t EltSize = sizeOf(EltTy);
      Result = APValue(APValue::UninitArray(), N, N);
      for (uint64_t I = 0; I != N; ++I)
        if (!read(EltTy, Buf + I * EltSize, Result.getArrayInitializedElt(I)))
          return false;
      return true;
    }
    if (const RecordDecl *RD = T->getAsRecordDecl()) {
      if (!checkRecord(Ty, RD))
        return false;
      const ASTRecordLayout &Layout = C.getASTRecordLayout(RD);
      const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD);
      unsigned NumBases = CXXRD ? CXXRD->getNumBases() : 0;
      unsigned NumFields = std::distance(RD->field_begin(), RD->field_end());
      Result = APValue(APValue::UninitStruct(), NumBases, NumFields);
      if (CXXRD) {
        unsigned I = 0;
        for (const CXXBaseSpecifier &B : CXXRD->bases()) {
          const CXXRecordDecl *BD = B.getType()->getAsCXXRecordDecl();
          if (!read(B.getType(),
                    Buf + Layout.getBaseClassOffset(BD).getQuantity(),
                    Result.getStructBase(I++)))
            return false;
        }
      }
      for (const FieldDecl *FD : RD->fields()) {
        if (FD->isUnnamedBitField())
          continue;
        if (FD->isBitField())
          return unsupported(Ty, "has a bit-field");
        if (!read(FD->getType(),
                  Buf + Layout.getFieldOffset(FD->getFieldIndex()) / 8,
                  Result.getStructField(FD->getFieldIndex())))
          return false;
      }
      return true;
    }
    return unsupported(Ty, "is not a type whose values can be returned yet");
  }
};

void callThunkWithErrorJump(ActiveHostState &Host, ConstevalThunkFn Thunk,
                            const void *const *ArgPtrs, void *RetBuf) {
  if (setjmp(Host.ErrorJump) == 0)
    Thunk(ArgPtrs, RetBuf);
}

/// Does everything between finding the thunk and having the result; returns
/// false with the failure recorded.
bool invoke(ActiveHostState &Host, PluginThunk &Thunk,
            llvm::ArrayRef<APValue> Args, APValue &Result) {
  ASTContext &Ctx = Host.Ctx;
  const FunctionDecl *FD = Host.FD;

  // The plugin was compiled against its own declarations of the parameter
  // and result types: they have to be laid out as the ones here are.
  if (!Thunk.Checked) {
    Thunk.Checked = true;
    std::string Here = getConstevalPluginSignature(Ctx, FD);
    if (!Thunk.Signature)
      Thunk.Mismatch = "the plugin does not say how it expects its arguments";
    else if (Here != Thunk.Signature)
      Thunk.Mismatch =
          ("its parameter and return types are not laid out here as they "
           "were where the plugin was built (here '" +
           Here + "', in the plugin '" + Thunk.Signature + "')");
  }
  if (!Thunk.Mismatch.empty())
    return Host.fail(Thunk.Mismatch);

  Marshaller M(Host);
  SmallVector<const void *, 8> ArgPtrs;
  auto Pass = [&](QualType Ty, const APValue &V, const llvm::Twine &What) {
    size_t Size = std::max<size_t>(8, M.sizeOf(Ty));
    uint8_t *Buf = Host.ScratchAlloc.Allocate<uint8_t>(Size);
    std::memset(Buf, 0, Size);
    if (!M.write(Ty, V, Buf))
      return Host.fail(What + " cannot be passed to a plugin: " + M.Reason);
    ArgPtrs.push_back(Buf);
    return true;
  };

  unsigned ArgIdx = 0;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD);
      MD && MD->isImplicitObjectMemberFunction()) {
    if (Args.empty())
      return Host.fail("the object the member function is called on is "
                       "missing");
    if (!Pass(Ctx.getCanonicalTagType(MD->getParent()), Args[ArgIdx++],
              "the object the member function is called on"))
      return false;
  }
  if (FD->isVariadic())
    return Host.fail("a variadic function cannot be called in a plugin");
  if (Args.size() - ArgIdx != FD->getNumParams())
    return Host.fail("the arguments do not match the parameters");
  for (const ParmVarDecl *PVD : FD->parameters()) {
    if (!Pass(PVD->getType().getNonReferenceType(), Args[ArgIdx++],
              "the argument for parameter " +
                  (PVD->getIdentifier()
                       ? "'" + PVD->getName() + "'"
                       : llvm::Twine(PVD->getFunctionScopeIndex() + 1))))
      return false;
  }

  QualType RetTy = FD->getReturnType();
  if (RetTy->isReferenceType())
    return Host.fail("a function returning a reference cannot be called in a "
                     "plugin");
  size_t RetSize =
      RetTy->isVoidType() ? 8 : std::max<size_t>(8, M.sizeOf(RetTy));
  uint8_t *RetBuf = Host.ScratchAlloc.Allocate<uint8_t>(RetSize);
  std::memset(RetBuf, 0, RetSize);

  callThunkWithErrorJump(Host, Thunk.Fn, ArgPtrs.data(), RetBuf);
  if (Host.Failed)
    return false;

  if (!M.read(RetTy, RetBuf, Result))
    return Host.Failed
               ? false
               : Host.fail("the result cannot be returned from a plugin: " +
                           M.Reason);
  return true;
}

} // namespace

bool clang::hasConstevalPluginSymbol(ASTContext &Ctx, const FunctionDecl *FD) {
  return lookupPluginThunk(Ctx, FD) != nullptr;
}

ConstevalPluginResult clang::invokeConstevalPlugin(
    ASTContext &Ctx, MetaActions &Meta, const FunctionDecl *FD,
    SourceLocation CallLoc, Decl *ContainingDecl, bool AllowInjection,
    Expr::EvalStatus *EvalStatus, ConstevalPluginHooks &Hooks,
    llvm::ArrayRef<APValue> Args, APValue &Result,
    ConstevalPluginFailure &Failure) {
  PluginThunk *Thunk = lookupPluginThunk(Ctx, FD);
  if (!Thunk)
    return ConstevalPluginResult::NotFound;

  llvm::TimeTraceScope TimeScope("ConstevalPlugin", [&] {
    return FD->getQualifiedNameAsString();
  });

  ActiveHostState Host(Ctx, Meta, FD, CallLoc, ContainingDecl, AllowInjection,
                       EvalStatus, Hooks, Failure);
  ActiveHostState *PrevHost = CurrentHost;
  CurrentHost = &Host;
  bool Ok = invoke(Host, *Thunk, Args, Result);
  CurrentHost = PrevHost;
  return Ok ? ConstevalPluginResult::Success : ConstevalPluginResult::Failure;
}
