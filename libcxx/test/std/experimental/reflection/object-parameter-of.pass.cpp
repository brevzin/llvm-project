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

// std::meta::object_parameter_of(f) is the object parameter of the non-static
// member function f: its explicit object parameter, if it has one, and
// otherwise its implicit object parameter ([over.match.funcs]), which is not
// among parameters_of(f).

#include <meta>
#include <string>
#include <utility>

using std::meta::info;

struct S {
  int v = 0;

  constexpr int f() { return v; }
  constexpr int fc() const { return v + 1; }
  constexpr int fv() volatile { return 2; }
  constexpr int fcv() const volatile { return 3; }
  constexpr int fl() & { return 4; }
  constexpr int fcl() const& { return 5; }
  constexpr int fr() && { return 6; }
  constexpr int fcr() const&& { return 7; }

  constexpr int self(this S& s) { return s.v + 10; }
  constexpr int self_val(this S s) { return s.v + 20; }
  template <class Self>
  constexpr int deduced(this Self&& s) { return s.v + 30; }

  constexpr int add(int a, int b) const { return v + a + b; }
};

struct V {
  virtual int virt() const { return 8; }
  operator int() const { return 9; }
  V& operator=(const V&);
};

// The type of the implicit object parameter is "lvalue reference to cv S" or,
// for '&&'-qualified functions, "rvalue reference to cv S".
static_assert(type_of(object_parameter_of(^^S::f)) == ^^S&);
static_assert(type_of(object_parameter_of(^^S::fc)) == ^^const S&);
static_assert(type_of(object_parameter_of(^^S::fv)) == ^^volatile S&);
static_assert(type_of(object_parameter_of(^^S::fcv)) == ^^const volatile S&);
static_assert(type_of(object_parameter_of(^^S::fl)) == ^^S&);
static_assert(type_of(object_parameter_of(^^S::fcl)) == ^^const S&);
static_assert(type_of(object_parameter_of(^^S::fr)) == ^^S&&);
static_assert(type_of(object_parameter_of(^^S::fcr)) == ^^const S&&);
static_assert(type_of(object_parameter_of(^^V::virt)) == ^^const V&);
static_assert(type_of(object_parameter_of(^^V::operator int)) == ^^const V&);
static_assert(type_of(object_parameter_of(^^V::operator=)) == ^^V&);

// The implicit object parameter is a function parameter of the function, but
// not an explicit object parameter, not among its parameters, and unnamed.
constexpr info ip = object_parameter_of(^^S::add);
static_assert(is_function_parameter(ip));
static_assert(!is_explicit_object_parameter(ip));
static_assert(!has_identifier(ip));
static_assert(!has_default_argument(ip));
static_assert(parent_of(ip) == ^^S::add);
static_assert(parameters_of(^^S::add).size() == 2);
static_assert(ip != parameters_of(^^S::add)[0]);
static_assert(is_complete_declaration(ip));
static_assert(source_location_of(ip).line() ==
              source_location_of(^^S::add).line());

// It is the same reflection each time it is asked for, and distinct between
// functions.
static_assert(object_parameter_of(^^S::add) == object_parameter_of(^^S::add));
static_assert(object_parameter_of(^^S::f) != object_parameter_of(^^S::fc));

// For an explicit object member function, it is the explicit object parameter.
static_assert(object_parameter_of(^^S::self) == parameters_of(^^S::self)[0]);
static_assert(is_explicit_object_parameter(object_parameter_of(^^S::self)));
static_assert(identifier_of(object_parameter_of(^^S::self)) == "s");
static_assert(type_of(object_parameter_of(^^S::self)) == ^^S&);
static_assert(type_of(object_parameter_of(^^S::self_val)) == ^^S);
static_assert(type_of(object_parameter_of(^^S::deduced<S const&>)) ==
              ^^const S&);
static_assert(type_of(object_parameter_of(^^S::deduced<S>)) == ^^S&&);

// All declarations of a function share their implicit object parameter.
struct R {
  void g() const;
  consteval static info first() { return ^^g; }
};
constexpr info r_first = R::first();
void R::g() const {}
static_assert(object_parameter_of(r_first) == object_parameter_of(^^R::g));

// Members of class template specializations and lambdas.
template <class T> struct Box {
  T t;
  T const& get() const& { return t; }
  T&& get() && { return std::move(t); }
};
consteval info get_overload(bool rvalue) {
  for (info m : members_of(^^Box<int>, std::meta::access_context::current()))
    if (is_function(m) && has_identifier(m) && identifier_of(m) == "get" &&
        is_rvalue_reference_qualified(m) == rvalue)
      return m;
  return {};
}
static_assert(type_of(object_parameter_of(get_overload(false))) ==
              ^^const Box<int>&);
static_assert(type_of(object_parameter_of(get_overload(true))) == ^^Box<int>&&);

constexpr auto lam = [](int x) { return x; };
static_assert(type_of(object_parameter_of(^^decltype(lam)::operator())) ==
              dealias(^^const decltype(lam)&));

// A reflection of an implicit object parameter can be a template argument.
template <info R> constexpr info param = R;
static_assert(param<object_parameter_of(^^S::fc)> ==
              object_parameter_of(^^S::fc));
static_assert(&param<object_parameter_of(^^S::fc)> ==
              &param<object_parameter_of(^^S::fc)>);
static_assert(&param<object_parameter_of(^^S::fc)> !=
              &param<object_parameter_of(^^S::fl)>);

// Generic member function invocation: whether f's object parameter is
// implicit or explicit, the receiver is given the type of the object parameter
// and passed along with that value category.
template <info F> struct invoke_member_t {
  using object = [: type_of(object_parameter_of(F)) :];

  template <class... Args>
  static constexpr decltype(auto) operator()(object obj, Args&&... args) {
    return static_cast<object>(obj).[:F:](std::forward<Args>(args)...);
  }
};
template <info F> constexpr invoke_member_t<F> invoke_member{};

constexpr bool test_invoke() {
  S s{.v = 100};
  S const& cs = s;
  if (invoke_member<^^S::f>(s) != 100) return false;
  if (invoke_member<^^S::fc>(cs) != 101) return false;
  if (invoke_member<^^S::fl>(s) != 4) return false;
  if (invoke_member<^^S::fr>(std::move(s)) != 6) return false;
  if (invoke_member<^^S::fcr>(std::move(cs)) != 7) return false;
  if (invoke_member<^^S::add>(cs, 1, 2) != 103) return false;
  if (invoke_member<^^S::self>(s) != 110) return false;
  if (invoke_member<^^S::self_val>(s) != 120) return false;
  if (invoke_member<^^S::deduced<S&>>(s) != 130) return false;
  if (invoke_member<^^S::deduced<S>>(S{.v = 1}) != 31) return false;
  return true;
}
static_assert(test_invoke());

// Substituting a template parameter holding one again (a concept-id that is
// not satisfied, checked again inside another template) does not crash.
consteval info fail(info r) {
  if (true)
    throw 0;
  return r;
}
template <info R>
concept C = requires { typename std::integral_constant<info, fail(R)>; };
template <info R, bool Expected> consteval bool check() {
  static_assert(C<R> == Expected);
  return true;
}
static_assert(check<object_parameter_of(^^S::fc), false>());

// Code generation for function template specializations on them.
template <info R> int tag() { return 0; }

struct D : V {
  int virt() const override { return 18; }
};

int main() {
  D d;
  V& v = d;
  if (invoke_member<^^V::virt>(v) != 18)
    return 1;
  if (invoke_member<^^V::operator int>(v) != 9)
    return 1;
  if (&tag<object_parameter_of(^^S::fc)> == &tag<object_parameter_of(^^S::fl)>)
    return 1;
  if (&tag<object_parameter_of(^^S::fc)> == &tag<parameters_of(^^S::add)[0]>)
    return 1;
  return tag<object_parameter_of(^^S::fc)>() + tag<object_parameter_of(^^S::self)>();
}
