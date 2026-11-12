// RUN: grep -Ev "// *[A-Z-]+:" %s \
// RUN:   | clang-format -style=LLVM \
// RUN:   | FileCheck -strict-whitespace %s

// In a requires-clause, '*' and '&' are taken to be binary operators -- except
// where nothing follows them, before '>', ',' or ')': there they can only be
// declarator operators (and this didn't crash on retyping one).

// CHECK:      {{^}}auto f = [](auto *p){{$}}
// CHECK-NEXT: {{^}}  requires(!C<decltype(p), char const *>){{$}}
// CHECK-NEXT: {{^}}{};{{$}}
// CHECK:      {{^}}auto g = [](auto &r){{$}}
// CHECK-NEXT: {{^}}  requires(C<int, char &>){{$}}
// CHECK-NEXT: {{^}}{};{{$}}
// CHECK:      {{^}}auto h = [](auto x){{$}}
// CHECK-NEXT: {{^}}  requires(x * 2 > 1){{$}}
// CHECK-NEXT: {{^}}{};{{$}}
auto f = [](auto *p) requires(!C<decltype(p), char const*>) {};
auto g = [](auto &r) requires(C<int, char&>) {};
auto h = [](auto x) requires(x*2 > 1) {};
