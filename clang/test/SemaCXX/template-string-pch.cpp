// RUN: %clang_cc1 -std=c++26 -freflection -emit-pch -o %t %s
// RUN: %clang_cc1 -std=c++26 -freflection -include-pch %t -fsyntax-only -verify %s
// expected-no-diagnostics

// Template string literals, and the templates containing them, must round-trip
// through a PCH: the generated struct, its consteval members, the processed
// pieces, and the implicit interpolation struct.

#ifndef HEADER
#define HEADER

// Template string literals return std::interpolation from their generated
// interpolation() members; the compiler looks the type up in the library
// (this test provides a minimal definition, like tests do for
// std::initializer_list or the comparison categories).
namespace std {
using size_t = decltype(sizeof(0));
struct interpolation {
  const char* expression;
  const char* fmt;
  size_t index;
  size_t count;
};
} // namespace std


int x = 1;

constexpr auto g = t"x={x:>{4}}";

template <class T>
constexpr auto make(T const &v) {
  return t"v={v}, sizeof={sizeof(T)}";
}

#else

static_assert(__builtin_strcmp(decltype(g)::fmt(), "x={:>{}}") == 0);
static_assert(decltype(g)::num_interpolations() == 1);
static_assert(__builtin_strcmp(decltype(g)::string(0), "x=") == 0);
static_assert(__builtin_strcmp(decltype(g)::interpolation(0).expression, "x") == 0);
static_assert(__builtin_strcmp(decltype(g)::interpolation(0).fmt, "{:>{}}") == 0);
static_assert(decltype(g)::interpolation(0).index == 0);
static_assert(decltype(g)::interpolation(0).count == 2);
static_assert(__is_same(decltype(g._0), int&));
static_assert(__is_same(decltype(g._1), int));

double d = 2.0;
using M = decltype(make(d));
static_assert(__builtin_strcmp(M::fmt(), "v={}, sizeof={}") == 0);
static_assert(__builtin_strcmp(M::interpolation(1).expression, "sizeof(T)") == 0);
static_assert(__is_same(decltype(M::_0), double const&));
static_assert(__is_same(decltype(M::_1), decltype(sizeof(0))));

#endif
