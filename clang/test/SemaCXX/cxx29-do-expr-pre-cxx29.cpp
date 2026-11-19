// RUN: %clang_cc1 -std=c++23 -verify=precxx29 -fsyntax-only %s
// RUN: %clang_cc1 -std=c++26 -verify=precxx29 -fsyntax-only %s
// RUN: %clang_cc1 -std=c++2d -verify=cxx29 -fsyntax-only %s
// cxx29-no-diagnostics

// P2806 ext: do-expressions require C++29. Below that, 'do' in expression
// position gets a targeted diagnostic pointing at the language mode.

int f() {
#if __cplusplus > 202400L
  return do { do return 42; };
#else
  int x = do { do return 42; }; // precxx29-error {{do-expressions are only available in C++29; use '-std=c++2d', or '-fdo-expressions' to enable them as an extension}}
  return x;
#endif
}

// There is no new keyword, so 'do_return' is an ordinary identifier in every
// mode, and 'do return' outside a do-expression is a do/while loop as ever.
int do_return = 1;
int g() {
  do return do_return; while (0);
}
