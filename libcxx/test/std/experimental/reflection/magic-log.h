//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// A logging macro that prefixes the message with the name of the object it is
// logged from, when there is one:
//
//   MAGIC_LOG("Hello")    is   LOG("[{}] Hello", get_name())
//                              inside a member of a class with a get_name(),
//                              and
//                         is   LOG("Hello")
//                              anywhere else
//
// written twice: (a) MAGIC_LOG, a C preprocessor macro, and (b) magic_log!, a
// token macro. Both find the function they are invoked in by walking out of
// any lambdas (is_closure_type), and reach the object through its object
// parameter (object_parameter_of), implicit or explicit. They differ in how
// they name the object.
//
// (b) emits only the code for the receiver it found: 'this->get_name()', or
// 'self.get_name()' with the explicit object parameter's name. Those tokens
// are looked up where the macro expands, so from inside a lambda they find
// the lambda's capture. It works everywhere, as long as a lambda in between
// captures the object and an explicit object parameter is named.
//
// (a) has to be the same code everywhere, for every receiver, with only the
// right branch taken. That rules out 'this', which is an error outside an
// implicit object member function even in a discarded statement. So:
//
// * For an explicit object parameter, it splices
//   variable_of(object_parameter_of(fn)) (or, for other receivers, a
//   placeholder variable). A splice may not name a local entity across an
//   intervening lambda, so this doesn't work from inside a lambda.
//
// * For the implicit object parameter, it calls get_name(none...), where
//   'none' is an empty pack of a generic lambda: a dependent call, looked up
//   again only if instantiated. But lookup where it is written already finds
//   a member get_name, if the class has one, and that suppresses
//   argument-dependent lookup. In an explicit object or static member
//   function, no 'this' can be the object argument, so if every get_name is a
//   non-static member function, no instantiation of the call can be valid,
//   and Clang rejects it right away, although it is discarded. So (a) doesn't
//   work in the explicit object and static member functions of a class with
//   get_name() -- even in a static member function, where there is no object
//   and it would only log the message.
//
// See magic-log.verify.cpp for what each rejects.

#ifndef MAGIC_LOG_H
#define MAGIC_LOG_H

#include <meta>
#include <debugging>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// ---------------------------------------------------------------- LOG ------

inline std::vector<std::string> logged;

template <class... Args>
void log_line(std::format_string<Args...> fmt, Args&&... args) {
  logged.push_back(std::format(fmt, std::forward<Args>(args)...));
}

#define LOG(fmt, ...) ::log_line(fmt __VA_OPT__(, ) __VA_ARGS__)

// ------------------------------------------------- what's in scope ---------

namespace magic {
using std::meta::info;

enum class receiver { none, implicit_object, explicit_object };

struct site {
  receiver kind = receiver::none;
  info object_parameter = {}; // for receiver::explicit_object
};

// The function, class, or namespace that 'scope' is in, outside any lambdas.
consteval info skip_closures(info scope) {
  while (is_function(scope) && is_type(parent_of(scope)) &&
         is_closure_type(parent_of(scope)))
    scope = parent_of(parent_of(scope));
  return scope;
}

// Whether, and how, code in 'scope' can call get_name() on an object.
consteval site find_receiver(info scope) {
  info fn = skip_closures(scope);
  if (!is_function(fn) || !is_class_member(fn) || is_static_member(fn))
    return {};
  if (!lookup(parent_of(fn), "get_name",
              std::meta::access_context::unchecked()))
    return {};
  // Constructors and destructors have no object parameter, but they do have
  // 'this'.
  if (is_constructor(fn) || is_destructor(fn))
    return {receiver::implicit_object};

  info p = object_parameter_of(fn);
  if (is_explicit_object_parameter(p))
    return {receiver::explicit_object, p};
  return {receiver::implicit_object};
}

// --------------------------------------------- (a) preprocessor macro ------

// Something (a) can splice for every receiver: the explicit object
// parameter's variable, if that is the receiver, and otherwise a placeholder.
inline constexpr int no_object = 0;
consteval info object_or_placeholder(site s) {
  return s.kind == receiver::explicit_object ? variable_of(s.object_parameter)
                                             : ^^no_object;
}
} // namespace magic

// The receiver is found before the generic lambda, so that the lambda isn't
// one of the scopes walked out of. Inside it, only the branch for that
// receiver is instantiated. The format string must be a string literal, to
// concatenate with the prefix.
#define MAGIC_LOG(fmt, ...)                                                    \
  do {                                                                         \
    constexpr ::magic::site magic_site_ =                                      \
        ::magic::find_receiver(::std::meta::access_context::current().scope());\
    [&](auto& magic_obj_, auto... magic_none_) -> void {                       \
      if constexpr (magic_site_.kind ==                                        \
                    ::magic::receiver::explicit_object)                        \
        LOG("[{}] " fmt, magic_obj_.get_name() __VA_OPT__(, ) __VA_ARGS__);    \
      else if constexpr (magic_site_.kind ==                                   \
                         ::magic::receiver::implicit_object)                   \
        LOG("[{}] " fmt, get_name(magic_none_...) __VA_OPT__(, ) __VA_ARGS__); \
      else                                                                     \
        LOG(fmt __VA_OPT__(, ) __VA_ARGS__);                                   \
    }([:::magic::object_or_placeholder(magic_site_):]);                        \
  } while (false)

// ------------------------------------------------- (b) token macro ---------

namespace magic {
// The contents of the constant string 'fmt' (a string literal, or a constant
// of a type such as std::string_view), with the prefix in front, as one new
// string literal. (Emitting the prefix next to 'fmt' would rely on the parser
// concatenating adjacent string literals, which happens before parsing, in
// translation phase 6, so not to the tokens a macro produces.)
consteval std::meta::token_sequence prefixed(std::meta::token_sequence fmt) {
  // (As a template argument, so that it must be a constant expression.)
  auto e = std::meta::test_expression(^^{
    ::std::integral_constant<const char*,
                             ::std::define_static_string(\(fmt))>::value
  });
  if (!e) {
    std::constexpr_error_str("magic-log",
                             "the format string must be a constant string");
    return {};
  }
  // (str_lit takes a string_view, but not a const char*.)
  std::string_view text = extract<const char*>(constant_of(*e));
  return ^^{ \(std::meta::str_lit("[{}] ", text)) };
}
} // namespace magic

__macro magic_log(std::meta::token_sequence fmt,
                  std::meta::token_sequence... args) {
  // In a template, look for get_name() in each instantiation: lookup into a
  // dependent class finds nothing.
  std::meta::expand_at_instantiation();

  using enum magic::receiver;
  magic::site s = magic::find_receiver(std::meta::macro_expansion_context());

  auto call = std::meta::list_builder(^^{ , });
  switch (s.kind) {
  case none:
    call += fmt;
    break;
  case implicit_object:
    call += magic::prefixed(fmt);
    call += ^^{ this->get_name() };
    break;
  case explicit_object:
    if (!has_identifier(s.object_parameter)) {
      std::constexpr_error_str(
          "magic-log", "the explicit object parameter must be named");
      return {};
    }
    call += magic::prefixed(fmt);
    call += ^^{ \(std::meta::id(identifier_of(s.object_parameter))).get_name() };
    break;
  }
  ((call += args), ...);
  return ^^{ ::log_line(\(call)) };
}

#endif // MAGIC_LOG_H
