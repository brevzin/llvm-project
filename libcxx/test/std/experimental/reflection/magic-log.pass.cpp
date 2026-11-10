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

// RUN: %{build}
// RUN: %{exec} %t.exe

// MAGIC_LOG and magic_log! (see magic-log.h) where they work: each prefixes
// the message with get_name() where that is a member of the class of the
// function it is invoked in, from inside lambdas too, and otherwise logs it
// as it is.

#include "magic-log.h"

#include <cassert>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

void free_function() {
  MAGIC_LOG("Hello");
  magic_log!("Hello");
}

struct Anonymous {
  void f() {
    MAGIC_LOG("Hello {}", 1);
    magic_log!("Hello {}", 1);
  }
};

// (a) works in a class without get_name() whatever the member.
struct AnonymousNoThis {
  void explicit_object(this AnonymousNoThis const&) {
    MAGIC_LOG("explicit");
    magic_log!("explicit");
  }
  static void static_member() {
    MAGIC_LOG("static");
    magic_log!("static");
  }
};

struct Widget {
  std::string name;

  std::string get_name() const { return name; }

  explicit Widget(std::string n) : name(std::move(n)) {
    MAGIC_LOG("constructed");
    magic_log!("constructed");
  }

  void implicit_object() const {
    MAGIC_LOG("Hello");
    magic_log!("Hello");
  }

  void from_lambdas() {
    [&] {
      MAGIC_LOG("in a lambda");
      magic_log!("in a lambda");
      [this]<class T>(T t) {
        MAGIC_LOG("in a generic lambda {}", t);
        magic_log!("in a generic lambda {}", t);
      }(2);
    }();
  }

  // Only (b), from here on: (a) is rejected in the explicit object and static
  // member functions of a class with get_name().
  void explicit_object(this Widget const& self, int x) {
    magic_log!("x={}", x);
  }

  // (The macro expands only in instantiations, so in the template, 'self'
  // looks unused.)
  template <class Self>
  void deduced([[maybe_unused]] this Self&& self) {
    magic_log!("deduced");
  }

  void explicit_from_lambda(this Widget const& self) {
    [&] { magic_log!("in a lambda"); }();
  }

  static void static_member() { magic_log!("static"); }

  // (b) takes any constant string as the format string; (a) takes only a
  // string literal, to concatenate with its prefix.
  void named_format_string() const {
    constexpr std::string_view fmt = "named {}";
    magic_log!(fmt, 3);
  }
};

template <class T>
struct Box {
  T t;
  std::string get_name() const { return std::format("box {}", t); }
  void f() {
    MAGIC_LOG("in a template");
    magic_log!("in a template");
  }
};

int main(int, char**) {
  auto expect = [](std::vector<std::string> lines) {
    assert(logged == lines);
    logged.clear();
  };

  free_function();
  expect({"Hello", "Hello"});

  Anonymous{}.f();
  expect({"Hello 1", "Hello 1"});

  AnonymousNoThis{}.explicit_object();
  AnonymousNoThis::static_member();
  expect({"explicit", "explicit", "static", "static"});

  Widget w("w");
  expect({"[w] constructed", "[w] constructed"});

  w.implicit_object();
  expect({"[w] Hello", "[w] Hello"});

  w.from_lambdas();
  expect({"[w] in a lambda", "[w] in a lambda", "[w] in a generic lambda 2",
          "[w] in a generic lambda 2"});

  w.explicit_object(42);
  expect({"[w] x=42"});

  w.deduced();
  Widget("tmp").deduced();
  expect({"[w] deduced", "[tmp] constructed", "[tmp] constructed",
          "[tmp] deduced"});

  w.explicit_from_lambda();
  expect({"[w] in a lambda"});

  Widget::static_member();
  expect({"static"});

  w.named_format_string();
  expect({"[w] named 3"});

  Box<int>{7}.f();
  expect({"[box 7] in a template", "[box 7] in a template"});

  return 0;
}
