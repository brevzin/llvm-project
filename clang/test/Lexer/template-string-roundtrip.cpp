// The contract this feature is designed around: compiling the preprocessed
// output is equivalent to compiling the source, and preprocessing is
// idempotent.
//
// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only %s
// RUN: %clang_cc1 -std=c++26 -freflection -E -P %s -o %t.ii
// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only -x c++ %t.ii
// RUN: %clang_cc1 -std=c++26 -freflection -E -P -x c++ %t.ii -o %t2.ii
// RUN: diff %t.ii %t2.ii

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


#define ANSWER 42
#define LOG_BODY t"answer={ANSWER=} width={w}"
#define NAMED(x) t"{x;#x}"

int w = 8;

constexpr int strsame(const char *a, const char *b) {
  return __builtin_strcmp(a, b) == 0;
}

// Macro constants keep their spelling through the synthesized name clause.
constexpr auto a = t"{ANSWER}";
static_assert(a._0 == 42);
static_assert(strsame(decltype(a)::fmt(), "{}"));
static_assert(strsame(decltype(a)::interpolation(0).expression, "ANSWER"));

// The trailing-'=' text is spelling-derived too.
constexpr auto b = t"{ANSWER = }, {ANSWER=:#x}";
static_assert(strsame(decltype(b)::fmt(), "ANSWER = {}, ANSWER={:#x}"));

// Template strings inside macro bodies, with nested fields.
auto c = LOG_BODY;
static_assert(strsame(decltype(c)::fmt(), "answer=ANSWER={} width={}"));
static_assert(strsame(decltype(c)::interpolation(0).expression, "ANSWER"));

// A user-written clause names the field explicitly; combined with '#' it
// captures the caller's spelling.
auto d = NAMED(w + 1);
static_assert(strsame(decltype(d)::interpolation(0).expression, "w + 1"));

// No clause is synthesized for a single clean token; spelling round-trips
// on its own.
constexpr auto e = t"{w:>{w}}";
static_assert(strsame(decltype(e)::fmt(), "{:>{}}"));
static_assert(strsame(decltype(e)::interpolation(0).expression, "w"));
static_assert(decltype(e)::interpolation(0).count == 2);
