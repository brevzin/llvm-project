// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only -verify=c1 -DCASE1 %s
// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only -verify=c2 -DCASE2 %s
// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only -verify=c3 -DCASE3 %s
// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only -verify=c4 -DCASE4 %s

// A template string literal is a single logical line; a terminator on
// another line means the literal was never closed, and recovery stays local
// to the statement (the rest of the offending line is consumed with the
// dead literal). One case per run so the cases cannot interact.

int x;

#ifdef CASE1
void f1() {
  // c1-error@+3 {{missing terminating '"' character in template string literal}}
  // c1-error@+2 {{expected ';' at end of declaration}}
  auto s1 = t"{x
}";
}
#endif

#ifdef CASE2
void f2() {
  // c2-error@+3 {{missing terminating '"' character in template string literal}}
  // c2-error@+2 {{expected ';' at end of declaration}}
  auto s2 = t"{x
:>8}";
}
#endif

#ifdef CASE3
void f3() {
  // c3-error@+4 {{missing terminating '"' character in template string literal}}
  // c3-error@+3 {{expected expression}}
  // c3-warning@+3 {{missing terminating '"' character}}
  // c3-error@+2 {{expected ';' at end of declaration}}
  auto s3 = t"abc
def";
}
#endif

#ifdef CASE4
void f4() {
  // c4-error@+1 {{'}' in a template string literal must be escaped as '}}'}}
  auto s4 = t"a}b";
}
#endif
