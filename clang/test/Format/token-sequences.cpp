// RUN: grep -Ev "// *[A-Z-]+:" %s \
// RUN:   | clang-format -style="{BasedOnStyle: LLVM, IndentWidth: 4, InsertBraces: true}" \
// RUN:   | FileCheck -strict-whitespace %s

// A token sequence literal, ^^{ ... }, is kept as written -- its contents are
// often not a complete construct -- and the code around it is formatted with
// it as an operand. A macro invocation, name!(...), is formatted as a call.

// CHECK:      {{^}}void f() {{{$}}
// CHECK-NEXT: {{^}}    x += ^^{ (this auto&& self };{{$}}
// CHECK-NEXT: {{^}}    l += ^^{ , };{{$}}
// CHECK-NEXT: {{^}}    if (is_tok(t, ^^{ typename })) {{{$}}
// CHECK-NEXT: {{^}}        y();{{$}}
// CHECK-NEXT: {{^}}    }{{$}}
// CHECK-NEXT: {{^}}    if (a) {{{$}}
// CHECK-NEXT: {{^}}        z = ^^{{{$}}
// CHECK-NEXT: {{^}}  if constexpr (b) return f( 1 );{{$}}
// CHECK-NEXT: {{^}}};{{$}}
// CHECK-NEXT: {{^}}    }{{$}}
// CHECK-NEXT: {{^}}    s = ^^{ "}" ${ x } };{{$}}
// CHECK-NEXT: {{^}}    each!(int i : v, { use(i); });{{$}}
// CHECK-NEXT: {{^}}    lib::declare!<double>(d);{{$}}
// CHECK-NEXT: {{^}}    return !(a);{{$}}
// CHECK-NEXT: {{^}}}{{$}}
void f() {
  x   +=   ^^{ (this auto&& self };
  l += ^^{ , };
  if (is_tok(t, ^^{ typename })) y();
  if (a) z = ^^{
  if constexpr (b) return f( 1 );
};
  s = ^^{ "}" ${ x } };
  each!(int i : v, {use(i);});
  lib::declare!<double>(d);
  return !(a);
}
