// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only -verify=missing %s
// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only -verify=malformed -DMALFORMED %s

// The generated interpolation() members return std::interpolation, which the
// compiler looks up in the library rather than inventing.

#ifdef MALFORMED
namespace std {
struct interpolation {
  const char* expression;
  int wrong;
};
} // namespace std
#endif

int x;
auto s = t"{x}";
// missing-error@-1 {{'std::interpolation' was not found; include <format> before using a template string literal}}
// malformed-error@-2 {{standard library implementation of 'std::interpolation' is not supported}}
