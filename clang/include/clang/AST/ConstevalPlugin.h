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
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace clang {

class ASTContext;
class Decl;
class FunctionDecl;
class MetaActions;

/// Kind of value passed across the consteval plugin C-ABI boundary.
enum ConstevalArgKind : uint32_t {
  CEK_Void = 0,
  CEK_Bool = 1,
  CEK_Int = 2,           // Val = signed/unsigned integer (up to 64 bits)
  CEK_MetaInfo = 3,      // Val = opaque handle (0 = null reflection)
  CEK_TokenSequence = 4, // Val = opaque handle (0 = empty token_sequence)
  CEK_MetaToken = 5,     // Val = opaque handle (single token)
  CEK_CharPtr = 6,       // Val = (uintptr_t)const char*, Len = optional len
  CEK_VoidPtr = 7,       // Val = (uintptr_t)void*
  CEK_MetaInfoPtr = 8,   // Val = (uintptr_t)uint64_t* (array of info handles)
  CEK_TokenPtr = 9,      // Val = (uintptr_t)uint64_t* (array of token handles)
  CEK_Char = 10,         // Val = a character (a part of an id or str_lit)
};

/// Uniform tagged value for metafunction / splice arguments and return values.
///
/// An integer or bool interpolated into a token sequence says what its type
/// is: Aux is its MetaTokenEnumKind, and (when that is None) Len points to
/// the description of the type (see encodeConstevalPluginReflection).
///
/// For a part of an id or str_lit: an integer has Aux set if it is unsigned;
/// a string has Aux set if Len is its length (otherwise it is terminated).
struct ConstevalArg {
  uint32_t Kind;
  uint32_t Aux;
  uint64_t Val;
  uint64_t Len;
};

/// Serialized token descriptor for lowering ^^{ ... } in a plugin TU.
struct ConstevalTokenDesc {
  uint16_t TokKind;   // tok::TokenKind (tok::annot_token_seq_expr for splice)
  uint16_t SpliceIdx; // index into Splices[] if TokKind == annot_token_seq_expr
  const char *Text;   // spelling for identifiers/literals (null for punctuators/keywords)
  uint32_t TextLen;
  uint32_t Flags; // Token::TokenFlags (leading space, start of line)
};

/// Host-compiler callback table provided to loaded consteval plugins via
/// __clang_consteval_init(const ConstevalHostAPI *).
///
/// A callback that fails does not return to the plugin: the invocation is
/// abandoned (see invokeConstevalPlugin) and the failure is diagnosed at the
/// call in the consumer. A plugin therefore never sees a null or made-up
/// result.
struct ConstevalHostAPI {
  uint32_t Version;
  uint32_t Reserved;

  /// Invoke compiler metafunction `MetaFnID` (Metafunction::Lookup).
  bool (*call_metafn)(unsigned MetaFnID, unsigned ResultKind,
                      const ConstevalArg *Args, size_t NumArgs,
                      ConstevalArg *Out);

  /// Materialize a token_sequence from serialized tokens + evaluated splices.
  uint64_t (*make_token_seq)(const ConstevalTokenDesc *Toks, size_t NumToks,
                             const ConstevalArg *Splices, size_t NumSplices);

  /// The reflection a reflect-expression in the plugin had, found again in
  /// the consumer's translation unit from its description (see
  /// encodeConstevalPluginReflection).
  uint64_t (*reflect)(const char *Desc, size_t Len);

  /// An exception left a plugin function.
  void (*unhandled_exception)();

  /// Equality of two reflections, as == on std::meta::info.
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

  /// The plugin reached something that could not be compiled for it (in a
  /// header, where it is no error unless it is reached).
  void (*unsupported)(const char *Msg, size_t Len);
};

/// The position of each member of ConstevalHostAPI, for the code a plugin
/// build generates to call it.
enum ConstevalHostField : unsigned {
  CEHF_Version,
  CEHF_Reserved,
  CEHF_CallMetafn,
  CEHF_MakeTokenSeq,
  CEHF_Reflect,
  CEHF_UnhandledException,
  CEHF_MetaEqual,
  CEHF_ReportDiag,
  CEHF_QueueInjection,
  CEHF_MakeIdOrStrLit,
  CEHF_StringizeTokens,
  CEHF_Unsupported,
  CEHF_NumFields
};

/// Signature of compiler-generated plugin entry-point thunks:
///   extern "C" void __clang_ce_thunk_<MangledName>(const void *const *Args,
///                                                  void *Ret);
using ConstevalThunkFn = void (*)(const void *const *ArgPtrs, void *RetPtr);

/// The symbols a plugin exports besides its thunks.
///   const char __clang_consteval_plugin_abi[]: getConstevalPluginABI() of the
///     compiler that built it.
///   void __clang_consteval_init(const ConstevalHostAPI *)
///   const char __clang_ce_sig_<MangledName>[]: getConstevalPluginSignature()
///     of the function the thunk of that name calls.
inline constexpr char ConstevalPluginABISymbol[] =
    "__clang_consteval_plugin_abi";
inline constexpr char ConstevalPluginInitSymbol[] = "__clang_consteval_init";
inline constexpr char ConstevalPluginThunkPrefix[] = "__clang_ce_thunk_";
inline constexpr char ConstevalPluginSignaturePrefix[] = "__clang_ce_sig_";

/// What a plugin and the compiler loading it must agree on: token kinds,
/// metafunction numbers, builtin type kinds and the host interface are all
/// those of one build of the compiler.
std::string getConstevalPluginABI();

/// How the parameters and the result of \p FD are laid out where they cross
/// into a plugin (each type's size, and what is at each offset). A plugin
/// records it for each function it exports; the consumer's declaration has to
/// give the same.
std::string getConstevalPluginSignature(ASTContext &Ctx,
                                        const FunctionDecl *FD);

/// Describe the reflection \p RV (the operand of a reflect-expression in a
/// plugin's source) so that the consumer's compiler can find the same thing
/// in its own translation unit. Returns false, with the reason in \p Error,
/// for a reflection that cannot be found from there: of something local to a
/// function or to the plugin's translation unit, or of a kind that has no
/// description yet.
bool encodeConstevalPluginReflection(ASTContext &Ctx, const APValue &RV,
                                     std::string &Desc, std::string &Error);

/// Whether \p T is, or has a subobject of type, std::meta::info, a token or
/// a token sequence.
bool isOrContainsMetaType(ASTContext &Ctx, QualType T);

/// What invoking a plugin needs of the constant evaluation it happens in.
class ConstevalPluginHooks {
public:
  virtual ~ConstevalPluginHooks();

  /// Read the array of \p CharTy that the pointer \p Ptr points into: its
  /// bytes, the index of the element \p Ptr points to, and a pointer to its
  /// first element. (A pointer to an object that is not an array element
  /// counts as into an array of one.) Returns false if \p Ptr does not point
  /// to something that can be read.
  virtual bool readCharArray(const APValue &Ptr, QualType CharTy,
                             SmallVectorImpl<char> &Bytes, uint64_t &Index,
                             APValue &First) = 0;

  /// A pointer to element \p Index of the array whose first element \p First
  /// points to.
  virtual bool offsetCharPointer(const APValue &First, QualType CharTy,
                                 uint64_t Index, APValue &Result) = 0;
};

/// Why a plugin invocation failed.
struct ConstevalPluginFailure {
  /// The plugin called std::consteval_error (or the like): Tag and Message
  /// are what it gave.
  bool IsUserError = false;
  std::string Tag;
  /// Otherwise, what went wrong crossing into or out of the plugin; empty if
  /// Notes says it all.
  std::string Message;
  /// The notes of a metafunction that failed.
  std::vector<PartialDiagnosticAt> Notes;
};

enum class ConstevalPluginResult {
  /// No loaded plugin defines the function.
  NotFound,
  Success,
  Failure,
};

/// Returns true if any loaded consteval plugin exports a thunk for `FD`.
bool hasConstevalPluginSymbol(ASTContext &Ctx, const FunctionDecl *FD);

/// Invoke the undefined consteval function (or macro) `FD` from a loaded
/// native consteval plugin. `Args` are the values of its parameters (for a
/// parameter of reference type, of what it refers to), preceded by the value
/// of the object for an implicit object member function.
ConstevalPluginResult
invokeConstevalPlugin(ASTContext &Ctx, MetaActions &Meta,
                      const FunctionDecl *FD, SourceLocation CallLoc,
                      Decl *ContainingDecl, bool AllowInjection,
                      Expr::EvalStatus *EvalStatus, ConstevalPluginHooks &Hooks,
                      llvm::ArrayRef<APValue> Args, APValue &Result,
                      ConstevalPluginFailure &Failure);

} // namespace clang

#endif // LLVM_CLANG_AST_CONSTEVALPLUGIN_H
