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
// ADDITIONAL_COMPILE_FLAGS: -std=c++29 -freflection

// P4340/P4341 ext: values whose subobjects have user-provided
// 'operator template's -- here, std::string and std::vector members holding
// transient allocations -- as annotations, as constant template arguments,
// and passed to reflect_constant. The subobjects are normalized while the
// evaluation that produced them is still live, so a long string (one that
// doesn't fit in the small buffer) works as well as a short one.
//
// Reported against Boost.ProgramOptions2's annotation-driven interface:
//   [[= argument("--iters", "width of each iteration")]] int width;
// failed with "pointer to subobject of heap-allocated object", but only once
// the help text reached 23 characters.

#include <meta>
#include <cassert>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

constexpr std::string_view Long = "a string too long for the small buffer";

// == annotations ============================================================

struct option {
  std::string names;
  std::string help;
  int args;
  std::vector<std::string> choices;
};

constexpr option argument(std::string names, std::string help) {
  return {std::move(names), std::move(help), 1, {}};
}
constexpr option argument(std::string help) { return argument("", std::move(help)); }

struct args {
  [[= argument("--iters", "number of iterations")]] int iterations = 1;
  [[= argument("width of each iteration")]] int width;
  [[= option{"-c", std::string(Long), 2, {"one", std::string(Long)}}]] int choice;
};

consteval option option_of(std::meta::info m) {
  return std::meta::extract<option>(annotations_of(m)[0]);
}
static_assert(option_of(^^args::iterations).help == "number of iterations");
static_assert(option_of(^^args::width).help == "width of each iteration");
static_assert(option_of(^^args::width).names.empty());
static_assert(option_of(^^args::choice).help == Long);
static_assert(option_of(^^args::choice).choices.size() == 2);
static_assert(option_of(^^args::choice).choices[1] == Long);

// The value of the annotation is the template parameter object of the
// normalized value.
static_assert(constant_of(annotations_of(^^args::width)[0]) ==
              std::meta::reflect_constant(argument("width of each iteration")));

// Dependent and non-dependent annotations in a template.
template <class T>
struct box {
  [[= argument(std::string(Long))]] T a;
  [[= argument(std::string(sizeof(T), '*') + std::string(Long))]] T b;
};
static_assert(option_of(^^box<int>::a).help == Long);
static_assert(option_of(^^box<char>::b).help == "*" + std::string(Long));
static_assert(option_of(^^box<int>::b).help == "****" + std::string(Long));

// == constant template arguments ============================================

template <option O>
struct opt_tag {
  static constexpr const char* help() { return O.help.c_str(); }
};

// Equal values name the same specialization; unequal ones don't.
static_assert(std::is_same_v<opt_tag<argument(std::string(Long))>, opt_tag<argument(std::string(Long))>>);
static_assert(!std::is_same_v<opt_tag<argument(std::string(Long))>, opt_tag<argument(std::string(Long) + "!")>>);
static_assert(std::string_view(opt_tag<argument(std::string(Long))>::help()) == Long);

template <auto V>
constexpr const auto* addr = &V;
static_assert(addr<argument(std::string(Long))> == addr<argument(std::string(Long))>);

// Bases, arrays, and nested aggregates are normalized, too.
struct base {
  std::string s;
};
struct derived : base {
  std::string arr[3];
  struct {
    std::vector<int> v;
  } inner;
};
template <derived D>
constexpr std::size_t total = D.s.size() + D.arr[0].size() + D.arr[1].size() + D.arr[2].size() + D.inner.v.size();
static_assert(total<derived{{std::string(Long)}, {std::string(Long), "x"}, {{1, 2, 3}}}> == 2 * Long.size() + 4);

// A defaulted 'operator template' normalizes member-wise as well.
struct defaulted {
  std::string s;
  consteval operator template() const = default;
};
template <defaulted D>
constexpr std::size_t size_of = D.s.size();
static_assert(size_of<defaulted{std::string(Long)}> == Long.size());

// == reflect_constant =======================================================

// At top level...
constexpr auto str = std::meta::reflect_constant(std::string(Long));
static_assert(std::meta::extract<const std::string&>(str) == Long);
static_assert(str == std::meta::reflect_constant(std::string(Long)));

// ...as an element of a container...
template <std::vector<std::string> V>
constexpr std::size_t count = V.size();
static_assert(count<std::vector<std::string>{"a", std::string(Long)}> == 2);
constexpr auto strs = std::meta::reflect_constant(std::vector<std::string>{"a", std::string(Long)});
static_assert(std::meta::extract<const std::vector<std::string>&>(strs)[1] == Long);

// ...and as a member.
constexpr auto opt = std::meta::reflect_constant(argument(std::string(Long)));
static_assert(std::meta::extract<const option&>(opt).help == Long);
static_assert(opt == std::meta::reflect_constant(argument(std::string(Long))));
static_assert(opt != std::meta::reflect_constant(argument(std::string(Long) + "!")));

// == extract ================================================================

// Extracting a value of a type with a non-trivial copy constructor copies it,
// rather than its representation: the copy owns its own storage.
consteval std::size_t extracted_size() {
  std::string s = std::meta::extract<std::string>(str);
  s += "!";
  option o = std::meta::extract<option>(opt);
  o.help += "!";
  return s.size() + o.help.size();
}
static_assert(extracted_size() == 2 * (Long.size() + 1));

int main(int, char**) {
  // The template parameter objects are usable at runtime.
  assert(std::string_view(opt_tag<argument(std::string(Long))>::help()) == Long);
  assert(addr<argument(std::string(Long))>->help == Long);
  assert((total<derived{{std::string(Long)}, {std::string(Long), "x"}, {{1, 2, 3}}}> == 2 * Long.size() + 4));

  static constexpr option width = option_of(^^args::width);
  assert(width.help == "width of each iteration");
  return 0;
}
