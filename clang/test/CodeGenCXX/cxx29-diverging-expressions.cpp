// RUN: %clang_cc1 -std=c++2d -triple x86_64-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm -o - %s | FileCheck %s

// P3549: std::noreturn_t, the type of a diverging expression.

namespace std { using noreturn_t = decltype(throw 0); }
using std::noreturn_t;

noreturn_t fail(const char *);

// A noreturn_t return is lowered like a void return (which is what lets
// noreturn_t(*)() convert to void(*)()), and the function is noreturn.
// CHECK: @vp = global ptr @_Z4failPKc
void (*vp)(const char *) = fail;

// CHECK: declare void @_Z4failPKc(ptr noundef) #[[NORETURN_DECL:[0-9]+]]

// CHECK: define {{.*}}void @_Z3defPKc(ptr noundef %m) #[[NORETURN_DEF:[0-9]+]]
// CHECK: call void @_Z4failPKc(ptr noundef %{{.*}}) #[[NORETURN_CALL:[0-9]+]]
// CHECK-NEXT: unreachable
noreturn_t def(const char *m) { fail(m); }

// A noreturn_t parameter occupies no argument slot.
// CHECK-LABEL: define {{.*}}void @_Z5takesiu10noreturn_ti(i32 noundef %a, i32 noundef %b)
void takes(int a, noreturn_t, int b) {}

// The diverging operand converts to the other operand's type without
// producing a value.
// CHECK-LABEL: define {{.*}}i32 @_Z4condb(
// CHECK: cond.false:
// CHECK-NEXT: call void @_Z4failPKc(ptr noundef @.str) #[[NORETURN_CALL]]
// CHECK-NEXT: unreachable
// CHECK: phi i32 [ 42, %cond.true ], [ poison,
int cond(bool c) { return c ? 42 : fail("x"); }

// An absorbed operator: the operands are evaluated, nothing is added.
// CHECK-LABEL: define {{.*}}i32 @_Z8absorbedi(
// CHECK: call noundef i32 @_Z3getv()
// CHECK-NEXT: call void @_Z4failPKc(ptr noundef @.str.1) #[[NORETURN_CALL]]
// CHECK-NEXT: unreachable
// CHECK-NOT: add
// CHECK: ret i32 poison
int get();
int absorbed(int) { return get() + fail("y"); }

// A reference bound to a diverging expression binds nothing.
// CHECK-LABEL: define {{.*}}ptr @_Z3refv(
// CHECK: call void @_Z4failPKc(ptr noundef @.str.2) #[[NORETURN_CALL]]
// CHECK-NEXT: unreachable
// CHECK: ret ptr poison
int &ref() { return fail("r"); }

// A vendor extended type in manglings.
// CHECK: declare void @_Z6manglePu10noreturn_t(ptr noundef)
void mangle(noreturn_t *);
void use_mangle(noreturn_t *p) { mangle(p); }

// CHECK-DAG: attributes #[[NORETURN_DEF]] = { {{.*}}noreturn
// CHECK-DAG: attributes #[[NORETURN_DECL]] = { noreturn
// CHECK-DAG: attributes #[[NORETURN_CALL]] = { noreturn }
