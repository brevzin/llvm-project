// RUN: %clang_cc1 -std=c++2d -triple x86_64-linux-gnu -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++2d -triple x86_64-windows-msvc -emit-llvm -o - %s | FileCheck %s --check-prefix=MS
// RUN: %clang_cc1 -std=c++2d -triple x86_64-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=NODECL
// RUN: %clang_cc1 -std=c++2d -triple x86_64-windows-msvc -emit-llvm -o - %s | FileCheck %s --check-prefixes=NODECL,MSGUARD

// What a do-expression body declares outside a function -- static locals,
// local classes, lambdas -- is named relative to the context Sema makes up for
// the body. Every such context used to mangle as the same '__do_expr_body()',
// so two of them clashed: "definition with same mangled name" for two local
// classes, and, worse, the lambdas of two specializations of a variable
// template silently shared one linkonce_odr definition.
//
// The context is now numbered like a lambda closure in the same place, and
// linked like one: vague linkage where the ODR makes translation units agree
// (inline and templated variables, default member initializers, default
// arguments in a class), internal otherwise.
//
// Not part of the Itanium ABI. The scheme is:
//   <source-name> ::= __do_expr_body [ _ <number> ]   # #1, #2, ...
//                 ::= __do_expr_body$_ <id>           # internal
// in the place a <closure-type-name> would go.

int id(int);

// Namespace scope, non-inline: internal, but distinct.
int ns1 = do { struct L { int f() { return id(1); } }; do return L{}.f(); };
int ns2 = do { struct L { int f() { return id(2); } }; do return L{}.f(); };
int ns3 = do { static int counter = id(3); do return ++counter; };
// CHECK-DAG: define internal noundef i32 @"_ZZ17__do_expr_body$_0vEN1L1fEv"(
// CHECK-DAG: define internal noundef i32 @"_ZZ17__do_expr_body$_1vEN1L1fEv"(
// CHECK-DAG: @"_ZZ17__do_expr_body$_2vE7counter" = internal global i32 0
// MS-DAG: define internal noundef i32 @"?f@L@?1??<do_expr_body_$0>@@YAXXZ@QEAAHXZ"(
// MS-DAG: define internal noundef i32 @"?f@L@?1??<do_expr_body_$1>@@YAXXZ@QEAAHXZ"(

// Inline variable: shared across translation units, numbered within it.
inline int inl = do { static int s = id(4); do return s; } +
                 do { static int s = id(5); do return s; };
int use_inl() { return inl; }
// CHECK-DAG: @_ZZN3inlM14__do_expr_bodyEvE1s = linkonce_odr global i32 0, comdat
// CHECK-DAG: @_ZGVZN3inlM14__do_expr_bodyEvE1s = linkonce_odr global i64 0, comdat
// CHECK-DAG: @_ZZN3inlM16__do_expr_body_1EvE1s = linkonce_odr global i32 0, comdat
// CHECK-DAG: @_ZGVZN3inlM16__do_expr_body_1EvE1s = linkonce_odr global i64 0, comdat
// MS-DAG: @"?s@?0??<do_expr_body_1>@inl@@YAXXZ@4HA" = linkonce_odr
// MS-DAG: @"?s@?0??<do_expr_body_2>@inl@@YAXXZ@4HA" = linkonce_odr

// A lambda in the body of an inline variable's do-expression, and a static
// local in that lambda: one of each across translation units.
inline auto get = do { auto l = [] { static int s; return &s; }; do return l; };
int *use_get() { return get(); }
// CHECK-DAG: define linkonce_odr noundef ptr @_ZZN3getM14__do_expr_bodyEvENKUlvE_clEv(
// CHECK-DAG: @_ZZZN3getM14__do_expr_bodyEvENKUlvE_clEvE1s = linkonce_odr global i32 0, comdat

// Variable template: one context per specialization.
template <class T> T vt = do { auto l = [] { return id(sizeof(T)); }; do return T(l()); };
long use_vt() { return vt<int> + vt<long>; }
// CHECK-DAG: define linkonce_odr noundef i32 @_ZZN2vtIiEM14__do_expr_bodyEvENKUlvE_clEv(
// CHECK-DAG: define linkonce_odr noundef i32 @_ZZN2vtIlEM14__do_expr_bodyEvENKUlvE_clEv(
// MS-DAG: define linkonce_odr dso_local noundef i32 @"??R<lambda_1>@?0??<do_expr_body_1>@?$vt@H@@YAXXZ@QEBA?A?<auto>@@XZ"(
// MS-DAG: define linkonce_odr dso_local noundef i32 @"??R<lambda_1>@?0??<do_expr_body_1>@?$vt@J@@YAXXZ@QEBA?A?<auto>@@XZ"(

// Default member initializers: one context per member.
struct A {
  int x = do { struct L { int f() { return id(6); } }; do return L{}.f(); };
  int y = do { struct L { int f() { return id(7); } }; do return L{}.f(); };
};
int use_a() { return A{}.x + A{}.y; }
// CHECK-DAG: define linkonce_odr noundef i32 @_ZZN1A1xM14__do_expr_bodyEvEN1L1fEv(
// CHECK-DAG: define linkonce_odr noundef i32 @_ZZN1A1yM14__do_expr_bodyEvEN1L1fEv(
// MS-DAG: define linkonce_odr dso_local noundef i32 @"?f@L@?2??<do_expr_body_1>@x@A@@YAXXZ@QEAAHXZ"(
// MS-DAG: define linkonce_odr dso_local noundef i32 @"?f@L@?4??<do_expr_body_1>@y@A@@YAXXZ@QEAAHXZ"(

template <class T> struct B {
  int x = do { struct L { int f() { return id(sizeof(T)); } }; do return L{}.f(); };
};
int use_b() { return B<char>{}.x + B<double>{}.x; }
// CHECK-DAG: define linkonce_odr noundef i32 @_ZZN1BIcE1xM14__do_expr_bodyEvEN1L1fEv(
// CHECK-DAG: define linkonce_odr noundef i32 @_ZZN1BIdE1xM14__do_expr_bodyEvEN1L1fEv(

// Default arguments in a class: one context per parameter, mangled the way a
// lambda there is ('Ed_' for the last parameter, 'Ed0_' for the one before).
// Each default argument is emitted at both calls, testing the one guard.
struct C {
  int g(int p = do { static int s = id(8); do return s++; },
        int q = do { static int s = id(9); do return s++; });
};
int use_c(C c) { return c.g() + c.g(); }
// CHECK-DAG: @_ZZZN1C1gEiiEd0_14__do_expr_bodyvE1s = linkonce_odr global i32 0, comdat
// CHECK-DAG: @_ZZZN1C1gEiiEd_14__do_expr_bodyvE1s = linkonce_odr global i32 0, comdat
// MS-DAG: @"?s@?4??<do_expr_body_1_1>@?g@C@@QEAAHHH@Z@YAXXZ@4HA" = linkonce_odr
// MS-DAG: @"?s@?2??<do_expr_body_2_1>@?g@C@@QEAAHHH@Z@YAXXZ@4HA" = linkonce_odr

// The made-up context is never itself emitted, or even declared.
// NODECL-NOT: declare {{.*}}do_expr_body

// MSVC gives each static local a guard variable of its own, which has to be
// the same one at every emission of its initialization, not a renamed copy.
// MSGUARD-NOT: @"?$TSS0{{.*}}.{{[0-9]+}}" =
