// RUN: %clang_cc1 -std=c++2d -verify=expected,warn -fsyntax-only %s
// RUN: not %clang_cc1 -std=c++2d -fsyntax-only -fdiagnostics-parseable-fixits %s 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only -Wno-do-return-while %s
// RUN: %clang_cc1 -std=c++23 -verify=precxx29 -fsyntax-only %s

// precxx29-no-diagnostics

// P2806 yields from a do-expression with 'do return', two existing keywords,
// rather than a new 'do_return' keyword that would break the existing uses of
// that name. The tokens 'do return' also begin a do/while loop whose body is a
// return statement; directly in the body of a do-expression they are a
// do-return statement, and everywhere else a do/while loop, as they always
// were. So no valid program today changes meaning.

int do_return(int x) { return x; }

#if __cplusplus > 202400L

int name_still_usable(int a) {
  return do { do return do_return(a); };
}

// Outside a do-expression: a do/while loop.
int loop_outside(int a) {
  do return a; while (0);
}

// In a lambda (or local class member) in a do-expression body, 'do return'
// could not reach the do-expression, so it is a do/while loop there too.
int loop_in_lambda(int a) {
  return do {
    auto l = [](int b) { do return b; while (b); };
    struct S {
      static int f(int b) { do return b; while (b); }
    };
    do return l(a) + S::f(a);
  };
}

// A 'do return' that is the body of a do/while loop is a do-return statement
// and yields from the do-expression; the 'while' belongs to the outer 'do'.
int body_of_loop(int a) {
  return do { do do return a; while (a); };
}

// Directly in the body, 'do return x;' is a do-return statement, so a 'while'
// after it is a separate loop -- not what the same tokens meant before
// do-expressions, so warn.
int yields_then_loops(int a) {
  return do {
    do return a; while (a); // warn-warning {{'do return' in a do-expression yields from it rather than beginning a do/while loop; this 'while' is a separate statement}} \
                            // warn-note {{add braces to write a do/while loop whose body is a return statement}}
  };
  // CHECK: fix-it:"{{.*}}":{[[@LINE-3]]:7-[[@LINE-3]]:7}:" {"
  // CHECK: fix-it:"{{.*}}":{[[@LINE-4]]:17-[[@LINE-4]]:17}:" }"
}

// The suggested fix: an ordinary do/while loop that returns from the function.
void braced_loop(int a) {
  (void)do { do { return; } while (a); };
}

void no_value(bool b) {
  (void)do { if (b) do return; do return; };
}

void misplaced() {
  do return; // expected-error {{'do return' can only appear inside a do-expression}}
}

int misplaced_in_lambda() {
  return do {
    [] { do return; }(); // expected-error {{'do return' cannot appear inside a nested function or lambda body of an enclosing do-expression}}
    do return 0;
  };
}

#else

// Without do-expressions, 'do return' is only ever a do/while loop.
int loop(int a) {
  do return do_return(a); while (0);
}

#endif
