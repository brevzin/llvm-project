//===--- Reflection.h - Classes for representing reflection -----*- C++ -*-===//
//
// Copyright 2024 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines facilities for representing reflected entities.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_REFLECTION_H
#define LLVM_CLANG_AST_REFLECTION_H

#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/AST/TypeBase.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include <optional>
#include <string>

namespace clang {

class APValue;
class ASTContext;
class CXXBaseSpecifier;
class NamedDecl;
class NamespaceDecl;
class Token;
class ValueDecl;

struct TagDataMemberSpec;
struct FunctionDeclSpec;

/// \brief The kind of construct reflected.
enum class ReflectionKind {
  /// \brief A null reflection.
  ///
  /// Corresponds to no object.
  Null = 0,

  /// \brief A reflection of a type.
  ///
  /// Corresponds to a QualType.
  Type,

  /// \brief A reflection of an object (i.e., the non-function result of an
  /// lvalue).
  ///
  /// Corresponds to an APValue (plus a QualType).
  Object,

  /// \brief A reflection of a value (i.e., the result of a prvalue).
  ///
  /// Corresponds to an APValue (plus a QualType).
  Value,

  /// \brief A reflection of a language construct that has a declaration in
  /// the Clang AST.
  ///
  /// Corresponds to a ValueDecl, which could be any of:
  /// - a variable (i.e., VarDecl),
  /// - a structured binding (i.e., BindingDecl),
  /// - a function (i.e., FunctionDecl),
  /// - an enumerator (i.e., EnumConstantDecl),
  /// - a non-static data member or unnamed bit-field (i.e., FieldDecl),
  Declaration,

  /// \brief A reflection of a template (e.g., class template, variable
  /// template, function template, alias template, concept).
  ///
  /// Corresponds to a TemplateName.
  Template,

  /// \brief A reflection of a namespace.
  ///
  /// Corresponds to a Decl, which could be any of:
  /// - the global namespace (i.e., TranslationUnitDecl),
  /// - a non-global namespace (i.e., NamespaceDecl),
  /// - a namespace alias (i.e., NamespaceAliasDecl)
  ///
  /// Somewhat annoyingly, these classes have no nearer common ancestor than
  /// the Decl class.
  Namespace,

  /// \brief A reflection of an entity proxy.
  ///
  /// Corresponds to a UsingShadowDecl.
  EntityProxy,

  /// \brief A reflection of a function parameter.
  ///
  /// Corresponds to a ParmVarDecl.
  Parameter,

  /// \brief A reflection of a base class specifier.
  ///
  /// Corresponds to a CXXBaseSpecifier.
  BaseSpecifier,

  /// \brief A reflection of a description of a hypothetical data member
  /// (static or nonstatic) that might belong to a class or union.
  ///
  /// Corresponds to a TagDataMemberSpec.
  ///
  /// This is specifically used for the 'std::meta::data_member_spec' and
  /// 'std::meta::define_class' metafunctions. If the surface area of
  /// 'define_class' grows (i.e., supports additional types of "descriptions",
  /// e.g., for member functions), it would be nice to find a more generic way
  /// to do this. One idea is to allow a reflection of a type erased struct,
  /// but the current design seems tolerable for now.
  DataMemberSpec,

  /// \brief A reflection of a description of a function declaration to be
  /// introduced into a class, cloned from an existing member function
  /// (std::meta::declaration_of).
  ///
  /// Corresponds to a FunctionDeclSpec.
  DeclarationSpec,

  /// \brief A reflection of an annotation (P2996 ext).
  Annotation,

  /// \brief A reflection of an expression bound to an expression-macro
  /// parameter. Only exists while a macro body is being evaluated.
  ///
  /// Corresponds to an Expr*.
  Expression,

  /// \brief A reflection of an overload set (P2996 ext): two or more
  /// functions and function templates, captured where the reflection was
  /// formed (^^f) or assembled (std::meta::make_overload_set).
  ///
  /// Corresponds to an OverloadSetData.
  OverloadSet,
};

/// \brief The candidates of a reflected overload set: two or more functions
/// and function templates (their canonical declarations, never a using
/// declaration's shadow), without duplicates, in declaration order. Uniqued in
/// the ASTContext, so reflections of the same set share one OverloadSetData.
class OverloadSetData : public llvm::FoldingSetNode {
  ArrayRef<NamedDecl *> Decls;

public:
  explicit OverloadSetData(ArrayRef<NamedDecl *> Decls) : Decls(Decls) {}
  ArrayRef<NamedDecl *> decls() const { return Decls; }

  void Profile(llvm::FoldingSetNodeID &ID) const { Profile(ID, Decls); }
  static void Profile(llvm::FoldingSetNodeID &ID, ArrayRef<NamedDecl *> Ds) {
    for (NamedDecl *D : Ds)
      ID.AddPointer(D);
  }
};

/// \brief Representation of a captured token sequence from ^^{ ... }.
/// Essentially an ArrayRef<Token> allocated in the ASTContext.
struct TokenSequenceData : public ArrayRef<Token> {
  TokenSequenceData() = default;
  TokenSequenceData(const Token *Data, size_t Length)
      : ArrayRef<Token>(Data, Length) {}
};

/// Allocate token sequence storage in the ASTContext. Empty sequences are
/// represented with an empty array.
/// std::meta::operators enumerates the overloadable operators in a fixed
/// order, with 0 meaning "no operator". These convert between that order and
/// OverloadedOperatorKind.
OverloadedOperatorKind getOverloadedOperatorForMetaIndex(unsigned Index);
unsigned getMetaIndexForOverloadedOperator(OverloadedOperatorKind OO);

/// std::meta::punctuator enumerates C++'s punctuators in a fixed order (see
/// MetaPunctuators.def). These convert between that order and token kinds;
/// a token kind that is not one of them has no index (~0u), and an index out
/// of range has no token kind (tok::unknown).
unsigned getMetaIndexForPunctuator(tok::TokenKind Kind);
tok::TokenKind getPunctuatorForMetaIndex(unsigned Index);

/// The overloadable operator a single token spells, or OO_None. Operators
/// without a one-token spelling ('()', '[]', new, delete) never match.
OverloadedOperatorKind getOverloadedOperatorForTokenKind(tok::TokenKind Kind);

/// Remove deduced placeholder sugar ('auto', 'decltype(auto)', deduced class
/// template names) from \p T so it can be spelled as a concrete type. The
/// deduced type's own sugar is kept when the placeholder is at the top level.
QualType stripDeducedTypeSugar(const ASTContext &C, QualType T);

TokenSequenceData CreateTokenSequenceData(ASTContext &Ctx,
                                          ArrayRef<Token> Tokens);

TokenSequenceData CreateTokenSequenceData(ASTContext &Ctx,
                                          ArrayRef<Token> Tokens1,
                                          ArrayRef<Token> Tokens2);

/// \p LHS followed by \p RHS, for std::meta::token_sequence's += and +.
/// Token arrays are immutable: an array is a view of a prefix of a buffer
/// that is only ever appended to. When \p LHS is a whole buffer's used
/// prefix and the buffer has room, \p RHS is written past it and the
/// result views the longer prefix -- every other view of the buffer still
/// sees exactly what it saw. Otherwise both are copied into a new buffer
/// with spare capacity. So building a sequence by appending is amortized
/// linear, and copies of a sequence never need copying themselves.
TokenSequenceData AppendTokenSequenceData(ASTContext &Ctx,
                                          TokenSequenceData LHS,
                                          ArrayRef<Token> RHS);

/// Allocate an empty token sequence in the ASTContext.
inline TokenSequenceData CreateEmptyTokenSequenceData(ASTContext &Ctx) {
  return CreateTokenSequenceData(Ctx, {});
}

/// The tokens of \p TSD as an array of std::meta::token in static storage
/// (what a std::meta::token_sequence iterates): a pointer to its first element,
/// of type \p PtrTy, or a null pointer for no tokens. The storage for a token
/// array is made once (keyed by its storage), and remembers its tokens (see
/// getMaterializedTokens).
APValue MaterializeTokens(ASTContext &C, TokenSequenceData TSD, QualType PtrTy);

/// The tokens of an array made by MaterializeTokens, or null.
const TokenSequenceData *getMaterializedTokens(const Decl *Storage);

/// \brief Representation of a hypothetical data member, which could be used to
/// complete an incomplete class definition using the 'std::meta::define_class'
/// standard library function.
struct TagDataMemberSpec {
  QualType Ty;

  std::optional<std::string> Name;
  std::optional<size_t> Alignment;
  std::optional<size_t> BitWidth;
  bool NoUniqueAddress;

  bool operator==(TagDataMemberSpec const& Rhs) const;
  bool operator!=(TagDataMemberSpec const& Rhs) const;
};

/// \brief Description of a function declaration to be introduced into a
/// class, cloned from an existing member function or member function
/// template (std::meta::declaration_of).
///
/// The description retains the *source* declaration and the naming policy;
/// the actual clone is produced at each injection site, so reusing the
/// description creates fresh declarations each time.
struct FunctionDeclSpec {
  /// The source: a CXXMethodDecl or a FunctionTemplateDecl whose templated
  /// declaration is a CXXMethodDecl. (For a description of a function type,
  /// an implicit prototype member of that type; see
  /// MetaActions::FunctionTypePrototype.)
  NamedDecl *Source;

  /// Replacement declaration name; empty means keep the source's name.
  std::optional<std::string> Name;

  /// Replacement operator function name (instead of an identifier).
  OverloadedOperatorKind NameOperator = OO_None;

  /// Prefixes for renaming template parameters (-> T0, T1, ...) and function
  /// parameters (-> p0, p1, ...).
  std::string TemplateParameterPrefix;
  std::string ParameterPrefix;

  /// Transformations applied to the description
  /// (std::meta::make_override / make_noexcept): declare the clone
  /// 'override', and/or declare it noexcept.
  bool MarkOverride = false;
  bool MarkNoexcept = false;
  /// std::meta::make_const: declare the clone a const member function.
  bool MarkConst = false;

  bool operator==(FunctionDeclSpec const &Rhs) const;
  bool operator!=(FunctionDeclSpec const &Rhs) const;
};

/// \brief Annotation payload for std::meta::template_parameter_list_for.
///
/// Carried by a tok::annot_template_param_spec token inside a token
/// sequence; when the parser reaches it inside a written template<...>
/// parameter list, the description's cloned (renamed) template parameters
/// are materialized at that position.
struct TemplateParamListSpec {
  FunctionDeclSpec *Spec;
  bool KeepDefaults;
};
} // namespace clang

#endif
