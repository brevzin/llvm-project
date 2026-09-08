//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection

// std::rebind_format (P3951 follow-on): pair a runtime pattern with the
// arguments of an existing template string. The result is a specialization
// of std::rebound_format, the vocabulary type the formatting functions
// accept alongside template strings themselves.

#include <cassert>
#include <concepts>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include "test_macros.h"
#include "assert_macros.h"
#include "concat_macros.h"

static void check_eq(std::string_view actual, std::string_view expected) {
  TEST_REQUIRE(expected == actual,
               TEST_WRITE_CONCATENATED("Expected output ", expected,
                                       "\nActual output  ", actual, '\n'));
}

int main(int, char**) {
  int x = 42;
  std::string who = "world";

  // The pattern may reorder, repeat, or drop the arguments.
  check_eq(std::format(std::rebind_format(
               std::string("{1}? x={0}, x again={0}"), t"x={x}, who={who}")),
           "world? x=42, x again=42");

  // The result is a specialization of the vocabulary type -- and is not
  // itself a template string (only literals are).
  static_assert(std::__is_rebound_format_v<
                decltype(std::rebind_format("{0}", t"{x}"))>);
  static_assert(!std::template_string<
                decltype(std::rebind_format("{0}", t"{x}"))>);

  // Pattern storage follows the argument: owned std::string, referenced
  // string_view / pointer.
  std::string owned = "[{0}]";
  std::string_view viewed = "<{0}>";
  check_eq(std::format(std::rebind_format(owned, t"{x}")), "[42]");
  check_eq(std::format(std::rebind_format(viewed, t"{x}")), "<42>");
  check_eq(std::format(std::rebind_format("({0})", t"{x}")), "(42)");

  // An owned pattern keeps the result self-contained (modulo the argument
  // references, as with any template string).
  auto make = [&](std::string decoration) {
    return std::rebind_format(decoration + "{0}" + decoration, t"{x}");
  };
  check_eq(std::format(make("**")), "**42**");

  // Rebinding composes by *flattening*: the new pattern replaces the old,
  // pairing directly with the original template string. The types do not
  // nest -- a rebind of a rebind has exactly the type of a single rebind.
  auto once = std::rebind_format(std::string("x is {0}"), t"{x}/{who}");
  auto twice = std::rebind_format(std::string("actually, {1} and {0}"), once);
  check_eq(std::format(once), "x is 42");
  check_eq(std::format(twice), "actually, world and 42");
  static_assert(std::same_as<decltype(twice), decltype(once)>);
  assert(twice.pattern() == "actually, {1} and {0}");

  // format_to works too.
  char buf[64];
  auto* end = std::format_to(buf, std::rebind_format("{0:>6}", t"{x}"));
  check_eq(std::string_view(buf, end - buf), "    42");

  // Specifier arguments (dynamic width) are part of the argument list like
  // any other, addressable from the new pattern.
  int width = 7;
  check_eq(std::format(std::rebind_format("{0:>{1}}", t"{x:>{width}}")),
           "     42");

  return 0;
}
