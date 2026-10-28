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

// overload(fs...) without inheritance: the object holds each callable, and
// its call operators are clones (declaration_of) of every call operator of
// every callable -- found by lookup -- each forwarding to the callable it
// came from. Overload resolution among them is ordinary overload resolution,
// so the best match wins, exactly as with 'struct overload : Fs... { using
// Fs::operator()...; }'. Unlike that, it also takes final classes and
// function pointers (whose call operator is declared from the pointee's
// function type).

#include <cassert>
#include <meta>
#include <string>
#include <tuple>
#include <type_traits>

using namespace std::meta;

template <class... Fs> struct overload {
  std::tuple<Fs...> fs;
  constexpr overload(Fs... f) : fs{f...} {}

  consteval {
    token_sequence ts;
    std::size_t i = 0;
    for (info F : {^^Fs...}) {
      // A function pointer: a call operator of its function type.
      if (is_pointer_type(F) && is_function_type(remove_pointer(F))) {
        auto d = make_const(
            declaration_of(remove_pointer(F), {.name = operators::op_parentheses}));
        ts += ^^{ \(d) { return std::get<\(i)>(fs)(\(argument_list_for(d))); } };
      } else if (auto call = lookup(F, operators::op_parentheses,
                                    access_context::unchecked())) {
        // A class: each of its call operators.
        for (info m : overloads_of(*call)) {
          auto d = declaration_of(m);
          ts += ^^{ \(d) { return std::get<\(i)>(fs)(\(argument_list_for(d))); } };
        }
      }
      ++i;
    }
    queue_injection(ts);
  }
};

template <class... Fs> overload(Fs...) -> overload<Fs...>;

template <class F, class... A>
concept callable_with = requires(F f, A... a) { f(a...); };

struct Named {
  constexpr int operator()(long) const { return 10; }
  constexpr int operator()(char const *) const { return 11; }
};
struct Final final {
  constexpr int operator()(char) const { return 20; }
};
constexpr int free_fn(int) { return 30; }

// Best match across lambdas, a class with overloads, and a constrained
// generic lambda (a member template with a deduced return type).
constexpr auto o = overload(
    [](int) { return 1; }, [](double) { return 2; },
    [](std::string const &) { return 3; }, Named{},
    [](auto *p) requires(!std::is_same_v<decltype(p), char const *>) {
      return 4;
    });
static_assert(o(1) == 1 && o(1.0) == 2 && o(1L) == 10 && o("lit") == 11);
static_assert(o(&o) == 4);
static_assert(!callable_with<decltype(o), Final>); // no viable candidate

// Final classes and function pointers.
constexpr auto p = overload(Final{}, &free_fn, [](double) { return 32; });
static_assert(p('c') == 20 && p(1) == 30 && p(1.5) == 32);

template <class... Fs> struct overload_macro {
  std::tuple<Fs...> fs;
  constexpr overload_macro(Fs... f) : fs{f...} {}

  consteval {
    std::size_t i = 0;
    for (info F : {^^Fs...}) {
      // The descriptions of F's call operators; each becomes a macro.
      std::vector<info> calls;
      if (is_pointer_type(F) && is_function_type(remove_pointer(F)))
        calls.push_back(make_const(declaration_of(
            remove_pointer(F), {.name = operators::op_parentheses})));
      else if (auto call = lookup(F, operators::op_parentheses,
                                  access_context::unchecked()))
        for (info m : overloads_of(*call))
          calls.push_back(declaration_of(m));
      // The expansion: the i-th callable, called with the caller's argument
      // expressions. ('$self' is the macro's object; '\\(...)' reaches out
      // to this generator.)
      for (info d : calls) {
        auto md = make_macro(d);
        auto args = argument_list_for(md);
        queue_injection(^^{ $md { return ^^{ std::get<$$i>($self.fs)($$args) }; } });
      }
      ++i;
    }
  }
};
template <class... Fs> overload_macro(Fs...) -> overload_macro<Fs...>;

// The same best matches as overload...
constexpr auto om = overload_macro(
    [](int) { return 1; }, [](double) { return 2; },
    [](std::string const &) { return 3; }, Named{},
    [](auto *p) requires(!std::is_same_v<decltype(p), char const *>) {
      return 4;
    });
static_assert(om(1) == 1 && om(1.0) == 2 && om(1L) == 10 && om("lit") == 11);
static_assert(om(&om) == 4);
static_assert(!callable_with<decltype(om), Final>);
// ... for final classes and function pointers too, and a non-movable
// parameter initialized from a prvalue (which a forwarding call operator
// can't do: it would have to move its parameter into the callable's), even
// when its callable is final.
struct Pinned {
  int v;
  constexpr Pinned(int v) : v(v) {}
  Pinned(Pinned &&) = delete;
};
constexpr Pinned make_pinned() { return Pinned(5); }
struct FinalPinned final {
  constexpr int operator()(Pinned pin) const { return pin.v * 10; }
};
constexpr auto pm = overload_macro(Final{}, &free_fn, FinalPinned{});
static_assert(pm('c') == 20 && pm(1) == 30 && pm(make_pinned()) == 50);
// A non-const call operator is not selected for a const object.
constexpr auto counting = [] {
  return overload_macro([count = 0](std::string const &) mutable {
                          return ++count;
                        },
                        [](int i) { return i; });
};
static_assert(callable_with<decltype(counting()) &, std::string>);
static_assert(!callable_with<decltype(counting()) const &, std::string>);
static_assert(callable_with<decltype(counting()) const &, int>);

int main(int, char **) {
  int calls = 0;
  auto r = overload([&calls](std::string const &) { return ++calls; },
                    [](int i) { return i; });
  assert(r(std::string("x")) == 1 && r(5) == 5 && calls == 1);
  auto rm = overload_macro([&calls](std::string const &) { return ++calls; },
                           [](int i) { return i; });
  assert(rm(std::string("x")) == 2 && rm(5) == 5 && calls == 2);
  return 0;
}
