// RUN: %clang_cc1 -std=c++26 -freflection -E %s | FileCheck %s

// Template string literals decompose into begin/middle/end tokens with
// ordinary tokens for the interpolated expressions, which are therefore
// macro-expanded in phase 4 like everything else. Spelling-derived data
// survives -E through the name clause ({expr;"text"}) that the preprocessor
// synthesizes; a single clean non-macro token needs none, since its spelling
// is its source text on every path.
#define ANSWER 42
#define BODY t"{x}"
#define CALL(x) t"{x;#x}"
int x;

auto s1 = t"v={x} {{lit}} {x:>{x}}";
// CHECK: auto s1 = t"v={x} {{[{][{]}}lit{{[}][}]}} {x:>{x}}";

auto s2 = t"{ANSWER}";
// CHECK: auto s2 = t"{42;"ANSWER"}";

auto s3 = t"{ANSWER=}, {ANSWER = :#x}";
// CHECK: auto s3 = t"{42=;"ANSWER="}, {42 =;"ANSWER = ":#x}";

// A template string in a macro body always carries a clause: whether 'x'
// names a macro cannot be known until each expansion.
auto s4 = BODY;
// CHECK: auto s4 = t"{x;"x"}";

// A user-written clause suppresses the synthesized one; '#' stringization
// gives a macro's callers their own spelling as the name.
auto s5 = CALL(x + 1);
// CHECK: auto s5 = t"{x + 1;"x + 1"}";
