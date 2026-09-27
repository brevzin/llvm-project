// RUN: %clang_cc1 -std=c++26 -freflection -triple x86_64-linux-gnu -emit-llvm -o - %s | FileCheck %s
// Copyright 2026 Jump Trading, LLC

// Each expansion of an expansion statement is a distinct entity, so static
// locals and local classes declared in its body must get distinct mangled
// names (rather than all sharing the mangling of the one pattern declaration),
// without colliding with same-named entities elsewhere in the function.

struct Call { int value; };

template <const Call &C> int read() { return C.value; }
template <class T> int id() { return sizeof(T); }

// Reported via Compiler Explorer: 'call' in both expansions got the same
// mangling, so both 'read<call>()' calls went to the same function.
template <int>
int sum() {
  static constexpr int values[] = {10, 20};
  int total = 0;
  template for (constexpr int v : values) {
    static constexpr Call call{v};
    total += read<call>();
  }
  return total;
}
int a = sum<0>();

// CHECK-DAG: @_ZZ3sumILi0EEivE4call = linkonce_odr {{.*}} { i32 10 }
// CHECK-DAG: @_ZZ3sumILi0EEivE4call_0 = linkonce_odr {{.*}} { i32 20 }
// CHECK-DAG: define linkonce_odr {{.*}} @_Z4readIL_ZZ3sumILi0EEivE4callEEiv()
// CHECK-DAG: define linkonce_odr {{.*}} @_Z4readIL_ZZ3sumILi0EEivE4call_0EEiv()

// Inline (non-template) function, with a same-named static local after the
// expansion statement.
inline int f() {
  int total = 0;
  template for (constexpr int v : {1, 2}) {
    static constexpr Call call{v};
    struct S { char c[v]; };
    total += read<call>() + id<S>();
  }
  { static constexpr Call call{3}; struct S {}; total += read<call>() + id<S>(); }
  return total;
}
int b = f();

// CHECK-DAG: @_ZZ1fvE4call = linkonce_odr {{.*}} { i32 1 }
// CHECK-DAG: @_ZZ1fvE4call_0 = linkonce_odr {{.*}} { i32 2 }
// CHECK-DAG: @_ZZ1fvE4call_1 = linkonce_odr {{.*}} { i32 3 }
// CHECK-DAG: define linkonce_odr {{.*}} @_Z2idIZ1fvE1SEiv()
// CHECK-DAG: define linkonce_odr {{.*}} @_Z2idIZ1fvE1S_0Eiv()
// CHECK-DAG: define linkonce_odr {{.*}} @_Z2idIZ1fvE1S_1Eiv()

// Function template, with same-named static locals before and after the
// expansion statement (whose numbers are forwarded from the pattern), and a
// nested expansion statement.
template <int N>
int g() {
  int total = 0;
  { static constexpr Call call{4}; total += read<call>(); }
  template for (constexpr int v : {10, 20}) {
    template for (constexpr int w : {v + 1, v + 2}) {
      static constexpr Call call{w + N};
      total += read<call>();
    }
  }
  { static constexpr Call call{5}; total += read<call>(); }
  return total;
}
int c = g<0>();

// CHECK-DAG: @_ZZ1gILi0EEivE4call = linkonce_odr {{.*}} { i32 4 }
// CHECK-DAG: @_ZZ1gILi0EEivE4call_0 = linkonce_odr {{.*}} { i32 5 }
// CHECK-DAG: @_ZZ1gILi0EEivE4call_1 = linkonce_odr {{.*}} { i32 11 }
// CHECK-DAG: @_ZZ1gILi0EEivE4call_2 = linkonce_odr {{.*}} { i32 12 }
// CHECK-DAG: @_ZZ1gILi0EEivE4call_3 = linkonce_odr {{.*}} { i32 21 }
// CHECK-DAG: @_ZZ1gILi0EEivE4call_4 = linkonce_odr {{.*}} { i32 22 }

// Static locals inside a member function of a local class are already
// distinguished by the (now distinctly-mangled) class.
inline int h() {
  int total = 0;
  template for (constexpr int v : {1, 2}) {
    struct L { static int get() { static int x = v; return x; } };
    total += L::get();
  }
  return total;
}
int d = h();

// CHECK-DAG: @_ZZZ1hvEN1L3getEvE1x = linkonce_odr
// CHECK-DAG: @_ZZZ1hvEN1L3getE_0vE1x = linkonce_odr
