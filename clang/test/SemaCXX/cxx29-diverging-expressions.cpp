// RUN: %clang_cc1 -std=c++2d -fsyntax-only -fcxx-exceptions -Wunreachable-code -Wreturn-type -Wunused-value -Wno-unevaluated-expression -verify %s
// RUN: %clang_cc1 -std=c++2d -fno-diverging-expressions -fsyntax-only -fcxx-exceptions -Wno-unevaluated-expression -verify=off %s -DOFF

// P3549 diverging expressions: the bottom type std::noreturn_t.

#ifdef OFF
// off-no-diagnostics
static_assert(__is_same(decltype(throw 0), void));
static_assert(!__has_feature(diverging_expressions));
#else

static_assert(__has_feature(diverging_expressions));
static_assert(__cpp_diverging_expressions == 1);

namespace std {
using noreturn_t = decltype(throw 0);
template <class T> T &&declval();
} // namespace std
using std::noreturn_t;

template <class T, class U> constexpr bool same = __is_same(T, U);

noreturn_t fail(const char *);
[[noreturn]] void fatal();
[[noreturn]] int fatal_int();
void log(const char *);

namespace the_type {
static_assert(sizeof(noreturn_t) == 1);
static_assert(__is_fundamental(noreturn_t));
static_assert(__is_object(noreturn_t));
static_assert(!__is_scalar(noreturn_t));
static_assert(!__is_empty(noreturn_t));
static_assert(__is_trivially_copyable(noreturn_t));
static_assert(__is_trivially_destructible(noreturn_t));
static_assert(!__builtin_is_implicit_lifetime(noreturn_t));
static_assert(!__is_constructible(noreturn_t));
static_assert(__is_constructible(noreturn_t, noreturn_t));
static_assert(__is_constructible(noreturn_t, const noreturn_t &));
static_assert(!__is_constructible(noreturn_t, int));

static_assert(__is_destructible(noreturn_t));
static_assert(__is_nothrow_destructible(noreturn_t));
// Pseudo-destructor calls work, as for any trivially destructible type
// (libstdc++'s is_destructible is built on them).
template <class T> auto pseudo_dtor(T &t) -> decltype(t.~T());
void destroy(noreturn_t *p) {
  using N = noreturn_t;
  p->~N();
  pseudo_dtor(*p);
}

noreturn_t n1;          // expected-error {{an object of type 'std::noreturn_t' can only be initialized by a diverging expression of that type}}
noreturn_t n2{};        // expected-error {{an object of type 'std::noreturn_t' can only be initialized by a diverging expression of that type}}
auto n3 = noreturn_t(); // expected-error {{an object of type 'std::noreturn_t' can only be initialized by a diverging expression of that type}}

void variables() {
  auto a = throw 1;
  static_assert(same<decltype(a), noreturn_t>);
  noreturn_t b = fail("b");
  noreturn_t c{fail("c")};
}

int bits = 0;
auto bc = __builtin_bit_cast(noreturn_t, (char)bits); // expected-error {{'__builtin_bit_cast' cannot produce a 'std::noreturn_t', which has no values}}
} // namespace the_type

namespace conversions {
struct NoCtors {
  NoCtors() = delete;
  NoCtors(const NoCtors &) = delete;
};
static_assert(__is_convertible(noreturn_t, int));
static_assert(__is_convertible(noreturn_t, int &));
static_assert(__is_convertible(noreturn_t, const int &));
static_assert(__is_convertible(noreturn_t, int &&));
static_assert(__is_convertible(noreturn_t, void));
// Directly, never through a constructor.
static_assert(__is_convertible(noreturn_t, NoCtors));
static_assert(!__is_convertible(int, noreturn_t));
static_assert(!__is_convertible(void, noreturn_t));

int i = fail("i");
int &r = fail("r");
NoCtors nc = fail("nc");
auto sc = static_cast<long>(throw 1);

// Exact Match, so several viable overloads are ambiguous.
void g(int);  // expected-note {{candidate}}
void g(long); // expected-note {{candidate}}
void use_g() { g(throw 1); } // expected-error {{call to 'g' is ambiguous}}
void g1(int);
void use_g1() { g1(throw 1); }

// A parameter of type noreturn_t: the argument diverges before the call.
template <class T> void h(T) {}
void use_h() { h(throw 1); }

// noreturn_t(*)(Args...) converts to void(*)(Args...): same ABI.
void (*vp)(const char *) = fail;
void (*vp2)(const char *) = &fail;
int (*ip)(const char *) = fail; // expected-error {{cannot initialize a variable of type 'int (*)(const char *)' with an lvalue of type 'noreturn_t (const char *)'}}
} // namespace conversions

namespace conditional {
// The result is the other operand: type, value category and all.
int x;
static_assert(same<decltype(true ? x : throw 1), int &>);
static_assert(same<decltype((true ? x : fail(""))), int &>);
static_assert(same<decltype(true ? 1 : fatal()), int>);
static_assert(same<decltype(true ? fatal() : 1L), long>);
static_assert(same<decltype(true ? throw 1 : fail("")), noreturn_t>);
static_assert(same<decltype(true ? log("") : fail("")), void>);
static_assert(same<decltype(true ? x : (log(""), throw 1)), int &>);
static_assert(same<decltype(false ? std::declval<int>() : std::declval<noreturn_t>()), int &&>);
static_assert(same<decltype(false ? std::declval<int &>() : std::declval<noreturn_t &>()), int &>);
struct B {
  int b : 3;
};
void bitfield(B &s, bool c) { (c ? s.b : throw 1) = 2; }
} // namespace conditional

namespace absorption {
struct S {
  int operator+(int);
};
struct NoOps {};
// An operand of type noreturn_t gives the expression that type, without any
// operator being looked up.
static_assert(same<decltype(1 + fail("")), noreturn_t>);
static_assert(same<decltype(fail("") + 1), noreturn_t>);
static_assert(same<decltype(S{} + fail("")), noreturn_t>);
static_assert(same<decltype(NoOps{} + fail("")), noreturn_t>);
static_assert(same<decltype(-fail("")), noreturn_t>);
static_assert(same<decltype(!fail("")), noreturn_t>);
static_assert(same<decltype((throw 1, 5)), noreturn_t>);
static_assert(same<decltype(fail("") && true), noreturn_t>);
// ... unless the operand might not be evaluated.
static_assert(same<decltype(true && fail("")), bool>);
static_assert(same<decltype(true || fail("")), bool>);
int arr[3];
static_assert(same<decltype(arr[fail("")]), noreturn_t>);
void assign(int &i) {
  static_assert(same<decltype(i = fail("")), noreturn_t>);
  static_assert(same<decltype(i += fail("")), noreturn_t>);
  1 + fail(""); // no unused-value warning
}
// In a template, even with a dependent other operand.
template <class T> auto dep(T t) { return t * fail(""); }
static_assert(same<decltype(dep(1)), noreturn_t>);
// 'throw' of a noreturn_t just diverges: 'throw (std::terminate(), 0)'.
static_assert(same<decltype(throw (fail(""), 0)), noreturn_t>);
} // namespace absorption

namespace do_exprs {
void f(bool c, int i) {
  static_assert(same<decltype(do { fail("a"); }), noreturn_t>);
  static_assert(same<decltype(do { log("x"); fail("a"); }), noreturn_t>);
  static_assert(same<decltype(do { log("x"); fatal(); }), noreturn_t>);
  static_assert(same<decltype(do { throw 1; }), noreturn_t>);
  static_assert(same<decltype(do { if (c) throw 1; else fatal(); }), noreturn_t>);
  static_assert(same<decltype(do { if (c) throw 1; fatal(); }), noreturn_t>);
  static_assert(same<decltype(do { if constexpr (true) fatal(); }), noreturn_t>);
  static_assert(same<decltype(do { if constexpr (false) fatal(); }), void>);
  static_assert(same<decltype(do { int x = fail("q"); }), noreturn_t>);
  static_assert(same<decltype(do { fatal(); ; }), noreturn_t>);
  // Diverging do_returns take no part in the deduction.
  static_assert(same<decltype(do { do_return fail("x"); }), noreturn_t>);
  static_assert(same<decltype(do { if (c) do_return 1; do_return fail("x"); }), int>);
  static_assert(same<decltype(do { if (c) do_return fail("x"); do_return 1; }), int>);
  static_assert(same<decltype(do { if (c) do_return fatal(); do_return 1; }), int>);
  static_assert(same<decltype(do { if (c) do_return fatal(); }), void>);
  static_assert(same<decltype(do -> auto { fatal(); }), noreturn_t>);
  static_assert(same<decltype(do { fatal_int() }), noreturn_t>);

  int j = c ? 1 : do { log("bye"); fatal(); };
  int k = do -> int { fatal(); };
  int &l = do -> int & { do_return fail("l"); };
  int m = do { if (c) do_return 1; else do_return fail("x"); };
  for (;;) {
    int n = i == 0 ? 0 : do { log("x"); continue; };
    (void)n;
  }
}
} // namespace do_exprs

namespace functions {
auto f1() { fatal(); }
static_assert(same<decltype(f1()), noreturn_t>);
auto f2() { throw 1; }
static_assert(same<decltype(f2()), noreturn_t>);
auto f3(bool c) {
  if (c)
    return 1;
  return fail("x");
}
static_assert(same<decltype(f3(true)), int>);
auto f4(bool c) {
  if (c)
    return fail("x");
  return 1;
}
static_assert(same<decltype(f4(true)), int>);
auto f5(bool c) {
  if (c)
    return fatal();
  return 2L;
}
static_assert(same<decltype(f5(true)), long>);
auto f6(bool c) {
  if (c)
    return fatal();
}
static_assert(same<decltype(f6(true)), void>);
auto f7() { return fail("x"); }
static_assert(same<decltype(f7()), noreturn_t>);
void f8() { return fail("x"); }
noreturn_t f9() { return fail("x"); } // no -Winvalid-noreturn
decltype(auto) f10() { fatal(); }
static_assert(same<decltype(f10()), noreturn_t>);

auto fatal_log = [](auto &&...args) {
  (log(args), ...);
  fatal();
};
static_assert(same<decltype(fatal_log("a", "b")), noreturn_t>);
auto l2 = [](bool c) {
  if (c)
    return 1;
  fatal();
};
static_assert(same<decltype(l2(true)), int>);

template <class T> auto t1(T t) {
  if (t)
    return t;
  throw 1;
}
static_assert(same<decltype(t1(1)), int>);
template <class T> auto t2(T) { fail("t"); }
static_assert(same<decltype(t2(1)), noreturn_t>);

// What follows a diverging operand is not reported as unreachable code.
struct Str {
  Str(const char *);
};
Str name(int i) { return i == 0 ? "zero" : fail(""); }
int sw(int i) {
  switch (i) {
  case 0:
    return 1;
  default:
    return fail("");
  }
}
void v() { return fail(""); }

// A function returning noreturn_t is noreturn.
int use(bool c) { return c ? 1 : f1(); }
int falls_off(bool c) {
  if (c)
    return 1;
  f1();
} // no -Wreturn-type
} // namespace functions

namespace constant_evaluation {
static_assert(__is_literal_type(noreturn_t));
constexpr noreturn_t bad(const char *) { throw 1; } // expected-note {{subexpression not valid in a constant expression}}
constexpr int f(int i) { return i >= 0 ? i : bad("negative"); } // expected-note {{in call to 'bad(&"negative"[0])'}}
static_assert(f(3) == 3);
constexpr int g = f(-1); // expected-error {{constexpr variable 'g' must be initialized by a constant expression}} \
                         // expected-note {{in call to 'f(-1)'}}
} // namespace constant_evaluation

#endif
