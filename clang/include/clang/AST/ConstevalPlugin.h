//===--- ConstevalPlugin.h - Native consteval plugin runtime ----*- C++ -*-===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief C-ABI host interface and host-side manager for independently
/// compiled native consteval plugins (-fconsteval-plugin /
/// -fconsteval-plugin-load=).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_CONSTEVALPLUGIN_H
#define LLVM_CLANG_AST_CONSTEVALPLUGIN_H

#include "clang/AST/APValue.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include <cstddef>
#include <cstdint>

namespace clang {

class ASTContext;
class Decl;
class FunctionDecl;
class MetaActions;

/// Kind of value passed across the consteval plugin C-ABI boundary.
enum ConstevalArgKind : uint32_t {
  CEK_Void = 0,
  CEK_Bool = 1,
  CEK_Int = 2,            // Val = signed/unsigned integer (up to 64 bits)
  CEK_MetaInfo = 3,       // Val = opaque handle (0 = null reflection)
  CEK_TokenSequence = 4,  // Val = opaque handle (0 = empty token_sequence)
  CEK_MetaToken = 5,      // Val = opaque handle (single token)
  CEK_CharPtr = 6,        // Val = (uintptr_t)const char*, Len = optional len
  CEK_VoidPtr = 7,        // Val = (uintptr_t)void*
  CEK_MetaInfoPtr = 8,    // Val = (uintptr_t)uint64_t* (array of info handles)
  CEK_TokenPtr = 9,       // Val = (uintptr_t)uint64_t* (array of token handles)
};

/// Uniform tagged value for metafunction / splice arguments and return values.
struct ConstevalArg {
  uint32_t Kind;
  uint32_t Aux; // e.g., bitwidth or signedness
  uint64_t Val;
  uint64_t Len;
};

/// Serialized token descriptor for lowering ^^{ ... } in a plugin TU.
struct ConstevalTokenDesc {
  uint16_t TokKind;   // tok::TokenKind (tok::annot_token_seq_expr for splice)
  uint16_t SpliceIdx; // index into Splices[] if TokKind == annot_token_seq_expr
  const char *Text;   // spelling for identifiers/literals (null for punctuators/keywords)
  uint32_t TextLen;
};

/// Host-compiler callback table provided to loaded consteval plugins via
/// __clang_consteval_init(const ConstevalHostAPI *).
struct ConstevalHostAPI {
  uint32_t Version;
  uint32_t Reserved;

  /// Invoke compiler metafunction `MetaFnID` (Metafunction::Lookup).
  /// Returns true on success, false if a consteval error occurred.
  bool (*call_metafn)(unsigned MetaFnID, unsigned ResultKind,
                      const ConstevalArg *Args, size_t NumArgs,
                      ConstevalArg *Out);

  /// Materialize a token_sequence from serialized tokens + evaluated splices.
  uint64_t (*make_token_seq)(const ConstevalTokenDesc *Toks, size_t NumToks,
                             const ConstevalArg *Splices, size_t NumSplices);

  /// Reflect a builtin type (BuiltinType::Kind) in the consumer ASTContext.
  uint64_t (*reflect_builtin)(unsigned BuiltinTypeKind);

  /// Reflect a named entity by qualified name in the consumer ASTContext.
  uint64_t (*reflect_named)(const char *QualName, size_t Len);

  /// Structural equality on two MetaInfo handles.
  bool (*meta_equal)(uint64_t LHS, uint64_t RHS);

  /// Report a consteval diagnostic (Kind: 0=print, 1=warning, 2=error).
  void (*report_diag)(uint32_t Kind, const char *Tag, size_t TagLen,
                      const char *Msg, size_t MsgLen);

  /// Queue a token_sequence for injection (std::meta::queue_injection).
  void (*queue_injection)(uint64_t TargetNSHandle, uint64_t TokenSeqHandle);

  /// Build an identifier token (IsStrLit=false) or string-literal token
  /// (IsStrLit=true) from concatenated string/int parts (std::meta::id/str_lit).
  uint64_t (*make_id_or_strlit)(bool IsStrLit, const ConstevalArg *Parts,
                                size_t NumParts);

  /// Stringize a token_sequence into a null-terminated C string (std::meta::stringize).
  const char *(*stringize_tokens)(uint64_t TokenSeqHandle);
};

/// Signature of compiler-generated plugin entry-point thunks:
///   extern "C" void __clang_ce_thunk_<MangledName>(const void *const *Args,
///                                                  void *Ret);
using ConstevalThunkFn = void (*)(const void *const *ArgPtrs, void *RetPtr);

/// Returns true if any loaded consteval plugin exports a thunk for `FD`.
bool hasConstevalPluginSymbol(ASTContext &Ctx, const FunctionDecl *FD);

/// Try to invoke an undefined consteval function `FD` from a loaded native
/// consteval plugin. Returns:
///   -1 if no loaded plugin defines `FD` (caller should emit normal diagnostic),
///    1 if the plugin ran and succeeded (result stored in `Result`),
///    0 if the plugin ran and failed with a diagnostic.
int invokeConstevalPlugin(ASTContext &Ctx, MetaActions &Meta,
                          const FunctionDecl *FD, SourceLocation CallLoc,
                          Decl *ContainingDecl, bool AllowInjection,
                          Expr::EvalStatus *EvalStatus,
                          llvm::ArrayRef<APValue> Args, APValue &Result,
                          std::string &ErrorTag, std::string &ErrorMsg);

} // namespace clang

#endif // LLVM_CLANG_AST_CONSTEVALPLUGIN_H
