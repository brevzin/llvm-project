// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s

// The implicit deduction guide for a non-template constructor must not shift
// the depth of the template parameter invented for a return-type-requirement
// in the constructor's trailing requires-clause (this used to assert with a
// negative template parameter depth).

template <class T, class U> concept conv = __is_convertible(T, U);

namespace non_template_ctor {
template <class D> struct Z { // #class
  Z(int z, D d) requires requires { { z + d } -> conv<D>; }; // #ctor
};

Z z1(1, 2.0);
static_assert(__is_same(decltype(z1), Z<double>));

struct S {};
Z z2(1, S{});
// expected-error@-1 {{no viable constructor or deduction guide for deduction of template arguments of 'Z'}}
// expected-note@#ctor {{candidate template ignored: constraints not satisfied [with D = S]}}
// expected-note@#ctor {{because 'z + d' would be invalid}}
// expected-note@#ctor {{implicit deduction guide declared as}}
// expected-note@#class {{candidate function template not viable: requires 1 argument, but 2 were provided}}
// expected-note@#class {{implicit deduction guide declared as}}

template <class... A>
constexpr bool deducible = requires (A... a) { Z(a...); };
static_assert(deducible<int, double>);
static_assert(!deducible<int, S>);

// 'z + d' is valid, but its type does not satisfy the type-constraint.
struct W {};
int operator+(int, W);
static_assert(!deducible<int, W>);
} // namespace non_template_ctor

namespace ctor_template {
template <class D> struct Z {
  template <class E>
  Z(D d, E e) requires requires { { d + e } -> conv<D>; };
};

Z z1(1.0, 2);
static_assert(__is_same(decltype(z1), Z<double>));

template <class... A>
constexpr bool deducible = requires (A... a) { Z(a...); };
static_assert(deducible<double, int>);
static_assert(!deducible<double, int*>);

struct W {};
int operator+(W, int);
static_assert(!deducible<W, int>);
} // namespace ctor_template
