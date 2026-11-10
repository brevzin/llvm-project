// RUN: %clang_cc1 %s -std=c++26 -fsyntax-only -verify

// P2758: __builtin_constexpr_diag(kind, tag, tag_len, msg, msg_len).
// Kind 0 = print (note), 1 = warning, 2 = error. Only a manifestly
// constant-evaluated call has an effect. A kind-2 call is constexpr-erroneous:
// it ends the evaluation (nothing after it is evaluated) and renders the
// program ill-formed, but the expression can still be a constant expression,
// so it is diagnosed once, by the message, and not again as non-constant.

constexpr int reject_zero(int a) {
  if (a == 0)
    __builtin_constexpr_diag(2, "", 0, "cannot call with a == 0", 23); // expected-error {{constexpr message: cannot call with a == 0}}
  return a;
}

// A failing branch that is never taken costs nothing, at definition time or
// at use.
constexpr int ok1 = reject_zero(2);
static_assert(reject_zero(3) == 3);

// Evaluating the error call emits the message, and nothing else: the variable
// is invalid, so neither it nor its uses are diagnosed again.
constexpr int bad1 = reject_zero(0);
static_assert(bad1 == 0);

// At runtime the call has no effect.
int runtime(int i) { return reject_zero(i); }

constexpr bool warn_odd(int a) {
  if (a % 2 == 1)
    __builtin_constexpr_diag(1, "odd-argument", 12, "argument is odd", 15); // expected-warning {{constexpr message with tag 'odd-argument': argument is odd}}
  return true;
}
static_assert(warn_odd(2));
static_assert(warn_odd(3));

constexpr bool say(int) {
  __builtin_constexpr_diag(0, "", 0, "hello from constant evaluation", 30); // expected-note {{constexpr message: hello from constant evaluation}}
  return true;
}
static_assert(say(1));

// The tag may only contain letters, digits, '_', '-', '='. A bad tag is
// misuse: the call is not a constant expression.
constexpr bool bad_tag() {
  __builtin_constexpr_diag(1, "no spaces", 9, "x", 1); // expected-note {{tag of a constexpr message may only contain letters, digits, '_', '-', and '='}}
  return true;
}
static_assert(bad_tag()); // expected-error {{static assertion expression is not an integral constant expression}} \
                          // expected-note {{in call to 'bad_tag()'}}

// Arity and argument types are checked.
void check_args() {
  __builtin_constexpr_diag(2, "", 0);            // expected-error {{too few arguments to function call, expected 5, have 3}}
  __builtin_constexpr_diag(2, 1, 0, "x", 1);     // expected-error {{argument 2 of '__builtin_constexpr_diag' must be a pointer to 'char'}}
  __builtin_constexpr_diag(2, "", 0, 1, 1);      // expected-error {{argument 4 of '__builtin_constexpr_diag' must be a pointer to 'char' or 'char8_t'}}
  __builtin_constexpr_diag("", "", 0, "x", 1);   // expected-error {{argument 1 of '__builtin_constexpr_diag' must be an integer}}
}

// char8_t messages are accepted (message argument only).
constexpr bool u8msg() {
  const char8_t* m = u8"utf-8";
  __builtin_constexpr_diag(0, "", 0, m, 5); // expected-note {{constexpr message: utf-8}}
  return true;
}
static_assert(u8msg());

// Nothing after an error call is evaluated: not another message, nor
// something that isn't a constant expression.
constexpr int stops(int a) {
  __builtin_constexpr_diag(0, "", 0, "before", 6); // expected-note {{constexpr message: before}}
  if (a == 0) {
    __builtin_constexpr_diag(2, "first", 5, "first error", 11); // expected-error {{constexpr message with tag 'first': first error}}
    __builtin_constexpr_diag(2, "second", 6, "second error", 12);
    __builtin_constexpr_diag(0, "", 0, "after", 5);
    return 1 / a;
  }
  return a;
}
constexpr int bad2 = stops(0);

// Every context that requires a constant expression reports the message once,
// and nothing more.
constexpr int fail(int a) {
  if (a == 0)
    __builtin_constexpr_diag(2, "fail", 4, "zero", 4); // expected-error 18 {{constexpr message with tag 'fail': zero}}
  return a;
}
int global = fail(0);           // not dynamically initialized instead
constinit int ci = fail(0);
static_assert(fail(0) == 0);
template <int N> struct T {};
T<fail(0)> t;
int arr[fail(0) + 1];           // not a variable length array
enum E { e = fail(0) };
struct S { int bf : fail(0) + 1; };
void ne() noexcept(fail(0));
alignas(fail(0) + 8) int al;
template <int N> constexpr int vt = fail(N);
int uv = vt<0>;
struct D {
  constexpr D(int a) : v(fail(a)) {}
  int v;
};
constexpr D d(0);
consteval int ce(int a) { return fail(a); }
int u = ce(0);                  // the initializer, not again the invocation
void locals(int x) {
  const int a = fail(0);
  static_assert(a == 0);
  ce(0); // expected-warning {{expression result unused}}
  int b = ce(1) + ce(0);
  if constexpr (fail(0)) {}
  switch (x) { case fail(0): break; }
  (void)b;
}
template <int N> requires (fail(N) == 0) void rq() {}
void use_rq() { rq<0>(); } // expected-error {{no matching function for call to 'rq'}}
                           // expected-note@-2 {{candidate template ignored: failed template argument deduction}}

// A constexpr function whose every evaluation reports an error still can be
// part of a constant expression: it is not diagnosed as never being one.
constexpr int always_fails() {
  __builtin_constexpr_diag(2, "", 0, "always", 6);
  return 0;
}

// Evaluations that don't require a constant expression stay silent, and the
// error still ends them: the call is not folded, but made at runtime, where
// it has no effect.
int at_runtime() { return fail(0) + reject_zero(0); }
