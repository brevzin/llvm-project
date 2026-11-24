//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -std=c++2d -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++23 -ftemplate-parameter-bounds -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++2d -fno-template-parameter-bounds -fsyntax-only -verify=off %s -DOFF

// P2481 bounded template type parameters. In 'typename T : U', T is deduced
// from a call argument as U with the argument's cv- and ref-qualifiers, as if
// the function parameter's type were U. Which conversions are allowed follows
// from U, as for any function parameter: where U has template parameters to
// deduce, the argument must be a U or derived from one; where it does not, the
// argument may be anything that converts to U.

#ifdef OFF
static_assert(!__has_feature(template_parameter_bounds));
struct Base {};
template <typename T : Base> void f(T &&); // off-error {{bounded template parameters are only available in C++29}}
#else

static_assert(__has_feature(template_parameter_bounds));
static_assert(__cpp_template_parameter_bounds == 202611L);

namespace std {
template <class T> struct remove_reference { using type = T; };
template <class T> struct remove_reference<T &> { using type = T; };
template <class T> struct remove_reference<T &&> { using type = T; };
template <class T> using remove_reference_t = typename remove_reference<T>::type;
template <class T> struct type_identity { using type = T; };
template <class T> using type_identity_t = typename type_identity<T>::type;
template <class T> T &&declval();
template <class T>
constexpr T &&forward(typename remove_reference<T>::type &t) {
  return static_cast<T &&>(t);
}
template <class T> constexpr T &&move(T &t) { return static_cast<T &&>(t); }
} // namespace std

template <class T, class U> constexpr bool same = false;
template <class T> constexpr bool same<T, T> = true;
template <class... Ts> struct list {};

struct Base { int v = 0; };
struct Derived : Base {};
struct Other {};
struct Str {
  Str(const char *);
  Str(int) = delete;
};

namespace known_bound {
// A bound with nothing to deduce: anything that converts to it.
template <typename T : Base> T &&f(T &&t) { return std::forward<T>(t); } // #base_f

Base b;
const Base cb{};
Derived d;
static_assert(same<decltype(f(b)), Base &>);
static_assert(same<decltype(f(cb)), const Base &>);
static_assert(same<decltype(f(Base{})), Base &&>);
// A derived argument binds to the base: no slicing.
static_assert(same<decltype(f(d)), Base &>);
static_assert(same<decltype(f(std::move(d))), Base &&>);
static_assert(same<decltype(f(static_cast<const Derived &>(d))), const Base &>);

template <typename T : Str> T &&g(T &&t) { return std::forward<T>(t); }
Str s = "";
static_assert(same<decltype(g(s)), Str &>);
static_assert(same<decltype(g(static_cast<const Str &>(s))), const Str &>);
static_assert(same<decltype(g(Str(""))), Str &&>);
// Anything else is converted at the call.
static_assert(same<decltype(g("hello")), Str &&>);

template <class T>
concept can_f = requires(T &&t) { f(std::forward<T>(t)); };
template <class T>
concept can_g = requires(T &&t) { g(std::forward<T>(t)); };
static_assert(can_f<Derived>);
static_assert(!can_f<Other>);
static_assert(can_g<const char *>);
static_assert(!can_g<int>);

void diagnose() {
  f(Other{}); // expected-error {{no matching function for call to 'f'}}
  // expected-note@#base_f {{candidate function [with T = Base] not viable: no known conversion from 'Other' to 'Base' for 1st argument}}
}

// By value: a derived argument is sliced, but there is one specialization.
template <typename T : Base> T by_value(T t) { return t; }
static_assert(same<decltype(by_value(d)), Base>);

// A braced-init-list initializes the bound.
struct Pair { int a, b; };
template <typename T : Pair> constexpr int first(T &&p) { return p.a; }
static_assert(first({1, 2}) == 1);

// 'class' works as well as 'typename', and a ':' before '::' needs a space.
template <class T : ::Base> void h(T &&);
static_assert(same<decltype(h(d)), void>);
} // namespace known_bound

namespace deduced_bound {
// A bound with template parameters to deduce: the argument must be a U or
// derived from one, and U's parameters are deduced from it.
template <class... Ts> struct tup {
  tup(Ts...);
  template <class... Us, typename Other : tup<Us...>>
    requires(sizeof...(Us) == sizeof...(Ts))
  tup(Other &&rhs) {
    static_assert(same<decltype(rhs), Other &&>);
  }
};
struct DT : tup<int, long> {
  using tup<int, long>::tup;
};

template <class... Us, typename Other : tup<Us...>> list<Other, Us...> f(Other &&); // #tup_f
tup<int, long> t(1, 2);
const tup<int, long> ct(1, 2);
DT dt(1, 2);
static_assert(same<decltype(f(t)), list<tup<int, long> &, int, long>>);
static_assert(same<decltype(f(std::move(ct))), list<const tup<int, long>, int, long>>);
static_assert(same<decltype(f(dt)), list<tup<int, long> &, int, long>>);

tup<long, long> t2 = t;
tup<long, long> t3 = DT(1, 2);

// Explicitly specifying what the bound names leaves nothing to deduce, so
// conversions are back, as for any function parameter.
struct ToTup { operator tup<int>() const; };
static_assert(same<decltype(f<int>(ToTup())), list<tup<int>, int>>);

void diagnose() {
  f(1); // expected-error {{no matching function for call to 'f'}}
  // expected-note@#tup_f {{candidate template ignored: could not match 'tup<Us...>' against 'int'}}
  f(ToTup()); // expected-error {{no matching function for call to 'f'}}
  // expected-note@#tup_f {{candidate template ignored: could not match 'tup<Us...>' against 'ToTup'}}
}

// The parameters a bound deduces must agree with what other parameters
// deduce, as usual.
template <class T, typename U : T> void g(T, U &&); // #g
void diagnose_g() {
  g(1, 2);
  g(Base{}, Derived{}); // expected-error {{no matching function for call to 'g'}}
  // expected-note@#g {{candidate template ignored: deduced conflicting types for parameter 'T' ('Base' vs. 'Derived')}}
}
} // namespace deduced_bound

namespace homogeneous_packs {
// P1219's deduced form: every element deduces T, which must agree.
template <class T, class... Ts : T> constexpr T sum(Ts... xs) { // #sum
  return (xs + ... + T());
}
static_assert(sum(1, 2, 3) == 6);

template <class... A>
concept can_sum = requires(A... a) { sum(a...); };
static_assert(can_sum<int, int>);
static_assert(!can_sum<int, long>);
static_assert(!can_sum<Base, Derived>);
static_assert(!can_sum<Derived, Base>);

// With T explicitly specified, there is nothing to deduce: each element
// converts, as in 'template <class T> void f(T, T); f<long>(1, 2.5)'.
static_assert(sum<double>(1, 2.5) == 3.5);
template <class T, class... Ts : T> list<Ts...> bases(Ts &&...); // #bases
Derived d;
static_assert(same<decltype(bases<Base>(d, Derived{}, Base{})),
                   list<Base &, Base, Base>>);

// Homogeneous forwarding: each element has its own value category.
template <class T, class... Ts : T> list<Ts...> fwd(Ts &&...);
int i;
static_assert(same<decltype(fwd(i, 42)), list<int &, int>>);

// P1219's other form, picking the type: a non-deduced context, so T comes
// from its default or an explicit argument, and the elements convert.
template <class T = int, class... Ts : std::type_identity_t<T>>
list<Ts...> pick(Ts...);
static_assert(same<decltype(pick(1, 2.5, 'c')), list<int, int, int>>);
static_assert(same<decltype(pick<long>(1, 2.5)), list<long, long>>);

void diagnose() {
  sum(1, 2L); // expected-error {{no matching function for call to 'sum'}}
  // expected-note@#sum {{candidate template ignored: deduced conflicting types for parameter 'T' ('int' vs. 'long')}}
  bases<Derived>(Base{}); // expected-error {{no matching function for call to 'bases'}}
  // expected-note@#bases {{candidate function [with T = Derived, Ts = <Derived>] not viable: no known conversion from 'Base' to 'Derived' for 1st argument}}
}
} // namespace homogeneous_packs

namespace self_reference {
// A parameter is in scope in its own bound, where it names the type deduced
// for it from the argument alone, in a non-deduced context.
template <class T> struct vec {
  using value_type = T;
  T *data();
};
template <class T> struct view {
  using value_type = T;
  template <class R>
    requires requires(R r) { r.data(); }
  view(R &&);
};
template <class R>
using value_t = typename std::remove_reference_t<R>::value_type;

template <typename R : view<value_t<R> const>> R f(R); // #self_f
vec<int> v;
static_assert(same<decltype(f(v)), view<int const>>);
static_assert(same<decltype(f(vec<char>())), view<char const>>);
static_assert(same<decltype(f(view<int const>(v))), view<int const>>);
static_assert(same<decltype(f<view<int const>>(v)), view<int const>>);

// Each element of a pack names itself.
template <typename... Rs : view<value_t<Rs> const>> list<Rs...> g(Rs &&...);
static_assert(same<decltype(g(v, vec<char>())),
                   list<view<int const>, view<char const>>>);

template <class E> struct Holder {
  template <typename R : view<value_t<R> const>> static R h(R);
};
static_assert(same<decltype(Holder<void>::h(v)), view<int const>>);

void diagnose() {
  f(1); // expected-error {{no matching function for call to 'f'}}
  // expected-note@#self_f {{candidate template ignored: substitution failure [with R = int]: type 'std::remove_reference_t<int>' (aka 'int') cannot be used prior to '::' because it has no members}}
  f<vec<int>>(v); // expected-error {{no matching function for call to 'f'}}
  // expected-note@#self_f {{candidate template ignored: substitution failure: template argument 'vec<int>' does not satisfy the bound 'view<const value_t<vec<int>>>' (aka 'view<const int>') of template parameter 'R'}}
}

// Naming itself where it would be deduced from the bound is circular.
template <typename T : vec<T>> void circular(T); // expected-error {{template parameter 'T' can only be named in its own bound in a non-deduced context}}

// The parameter is not in scope in its default argument.
using X = int;
template <typename X : int = X> constexpr int dflt() { return 0; }
static_assert(dflt() == 0);
} // namespace self_reference

namespace deducing_this {
struct Widget {
  template <typename Self : Widget> Self &&get(this Self &&self) {
    return std::forward<Self>(self);
  }
};
struct Gadget : Widget {};
Gadget g;
static_assert(same<decltype(g.get()), Widget &>);
static_assert(same<decltype(Gadget().get()), Widget &&>);

template <class X> struct Crtp {
  template <typename Self : Crtp> Self &self(this Self &s) { return s; }
};
struct Leaf : Crtp<int> {};
Leaf leaf;
static_assert(same<decltype(leaf.self()), Crtp<int> &>);
} // namespace deducing_this

namespace explicit_and_default_arguments {
template <typename T : Base> void f(T &&); // #f
void test() {
  f<Base &>(std::declval<Derived &>());
  f<const Base>(Derived{});
  f<Derived &>(std::declval<Derived &>()); // expected-error {{no matching function for call to 'f'}}
  // expected-note@#f {{candidate template ignored: substitution failure: template argument 'Derived &' does not satisfy the bound 'Base' of template parameter 'T'; it must be the bound, optionally cv- or ref-qualified}}
}

template <typename T : int = int> constexpr int dflt() { return sizeof(T); }
static_assert(dflt() == sizeof(int));
template <typename T : int = long> constexpr int bad_dflt() { return 0; } // #bad_dflt
static_assert(bad_dflt() == 0); // expected-error {{no matching function for call to 'bad_dflt'}}
// expected-note@#bad_dflt {{candidate template ignored: substitution failure [with T = long]: template argument 'long' does not satisfy the bound 'int' of template parameter 'T'}}

// A bound deduces what it names in deduced contexts, as a parameter would.
template <class T, typename U : T> T m(U);
static_assert(same<decltype(m(1)), int>);
template <class T, typename U : std::type_identity_t<T>> void n(U); // #n
void test_n() {
  n<long>(1);
  n(1); // expected-error {{no matching function for call to 'n'}}
  // expected-note@#n {{candidate template ignored: couldn't infer template argument 'T'}}
}
} // namespace explicit_and_default_arguments

namespace class_templates {
// For a class template, the bound just checks the argument.
template <typename T : Base> struct S {}; // expected-note {{template parameter is declared here}}
S<Base> s1;
S<const Base &> s2;
S<Derived> s3; // expected-error {{template argument 'Derived' does not satisfy the bound 'Base' of template parameter 'T'; it must be the bound, optionally cv- or ref-qualified}}
} // namespace class_templates

namespace instantiation {
template <class X> struct W {
  template <typename T : X> static T &&id(T &&t) { return std::forward<T>(t); }
};
static_assert(same<decltype(W<Base>::id(std::declval<Derived &>())), Base &>);
static_assert(same<decltype(W<long>::id(1)), long &&>);

template <class X> struct O { // expected-note {{O defined here}}
  template <typename T : X> int f(T &&);
  template <typename T : X> int g(T &&);
};
template <class X> template <typename T : X> int O<X>::f(T &&) { return 1; }
// As with constraints, a different bound declares a different template.
template <class X> template <typename T> int O<X>::g(T &&) { return 1; } // expected-error {{out-of-line definition of 'g' does not match any declaration in 'instantiation::O<X>'}}
int o = O<Base>().f(Derived{});

template <class X> struct Ref {
  template <typename T : X> void f(); // expected-error {{bound 'int &' of a template parameter cannot be a reference type}}
};
Ref<int &> r; // expected-note {{in instantiation of template class 'instantiation::Ref<int &>' requested here}}

auto lambda = []<typename T : Base>(T &&t) -> T && { return std::forward<T>(t); };
static_assert(same<decltype(lambda(std::declval<const Derived &>())), const Base &>);
} // namespace instantiation

namespace redeclaration {
template <typename T : Base> void f(T);
template <typename T : Base> void f(T) {}
template <typename T : Base> void f(T); // overloads differing only in the bound are distinct
template <typename T : Other> void f(T);
template <typename T> void f(T);

template <typename T : Base> struct S; // expected-note {{previous template declaration is here}}
template <typename T : Other> struct S; // expected-error {{template parameter bound differs in template redeclaration}}
} // namespace redeclaration

namespace partial_ordering {
// A bound acts like a constraint: it wins a tie with an unbounded template.
template <class T> constexpr int f(T &&) { return 0; }
template <typename T : Base> constexpr int f(T &&) { return 1; }
static_assert(f(Base{}) == 1);
static_assert(f(Derived{}) == 0); // the unbounded T = Derived is an exact match
static_assert(f(1) == 0);

template <typename T : Base> constexpr int h(T &&) { return 1; }
template <typename T : Derived> constexpr int h(T &&) { return 2; }
static_assert(h(Base{}) == 1);
static_assert(h(Derived{}) == 2);

// A non-template is still preferred when the conversions tie.
constexpr int k(const Base &) { return 1; }
template <typename T : Base> constexpr int k(T &&) { return 2; }
constexpr Base cb{};
static_assert(k(cb) == 1);
static_assert(k(Derived{}) == 2);
} // namespace partial_ordering

namespace invalid_bounds {
template <typename T : Base &> void f(); // expected-error {{bound 'Base &' of a template parameter cannot be a reference type}}
template <typename T : const Base> void g(); // expected-error {{bound 'const Base' of a template parameter cannot be cv-qualified}}
template <typename T : void> void h(); // expected-error {{bound 'void' of a template parameter must be an object type}}
template <typename T : int()> void i(); // expected-error {{bound 'int ()' of a template parameter must be an object type}}
template <class... Ts> struct P {
  template <typename U : Ts> void f(); // expected-error {{template parameter bound contains unexpanded parameter pack 'Ts'}}
};
} // namespace invalid_bounds

#endif
