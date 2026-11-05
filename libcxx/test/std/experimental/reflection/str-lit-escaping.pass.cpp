//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// std::meta::str_lit(s) is the string literal whose value is s: its spelling
// escapes '"', '\', control characters, and bytes that aren't valid UTF-8, so
// the literal round-trips whatever s contains.

#include <meta>
#include <string_view>

using std::meta::str_lit;
using std::meta::token_sequence;
using namespace std::string_view_literals;

// The literal, as an expression.
__macro lit(std::string_view s) {
  return ^^{ \(str_lit(extract<std::string_view>(constant_of(s)))) };
}

consteval auto spelling(std::string_view s) -> std::string_view {
  return stringize(token_sequence{str_lit(s)});
}

// Nothing to escape.
static_assert(lit!("plain") == "plain"sv);
static_assert(spelling("plain") == R"("plain")"sv);

// Quotes and backslashes.
static_assert(lit!("a\"b\\c") == "a\"b\\c"sv);
static_assert(sizeof(lit!("a\"b\\c")) == 6);  // 5 characters and a NUL
static_assert(spelling("a\"b\\c") == R"("a\"b\\c")"sv);
static_assert(lit!("\\") == "\\"sv);
static_assert(lit!("\"") == "\""sv);

// Control characters: named escapes where there are any, octal otherwise
// (three digits, so a following digit is its own character).
static_assert(lit!("tab\there\nnl") == "tab\there\nnl"sv);
static_assert(spelling("a\tb\nc") == R"("a\tb\nc")"sv);
static_assert(lit!("\x01" "1\x7f") == "\x01" "1\x7f"sv);
static_assert(spelling("\x01" "1") == R"("\0011")"sv);

// Valid UTF-8 is kept as written; a stray byte is escaped (so the literal
// doesn't draw an invalid-encoding warning).
static_assert(lit!("caf\u00e9") == "caf\u00e9"sv);
static_assert(spelling("caf\u00e9") == "\"caf\u00e9\""sv);
static_assert(lit!("\xff" "1") == "\xff" "1"sv);
static_assert(spelling("\xc3" "1") == R"("\3031")"sv);

// The motivating case: a user-defined literal made by lexing the literal's
// spelling with a suffix, as one token.
consteval auto operator""_len(const char*, std::size_t n) -> std::size_t {
  return n;
}
__macro len(std::string_view s) {
  return std::meta::tokenize(
      spelling(extract<std::string_view>(constant_of(s))), "_len");
}
static_assert(len!("a\"b\\c") == 5);

int main() {}
