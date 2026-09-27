// RUN: %clang_cc1 -std=c++17 -triple x86_64-linux-gnu -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++26 -triple x86_64-linux-gnu -emit-llvm -o - %s | FileCheck %s
// Copyright 2026 Jump Trading LLC

struct Call { int value; };
template <const Call &C> int read() { return C.value; }
template <const Call &C> const Call *address() { return &C; }

// Both functions must refer to the instantiated variable, not the local in
// the template pattern. The latter used to emit a zero-initialized global for
// read<call>(), and could crash codegen for address<call>().
template <int> int test() {
  static constexpr Call call{4};
  return address<call>() == &call ? read<call>() : -1;
}
int use() { return test<0>() + test<1>(); }

// CHECK-DAG: @_ZZ4testILi0EEivE4call = linkonce_odr {{.*}} { i32 4 }
// CHECK-DAG: @_ZZ4testILi1EEivE4call = linkonce_odr {{.*}} { i32 4 }
// CHECK-DAG: call {{.*}} @_Z7addressIL_ZZ4testILi0EEivE4callEEPK4Callv()
// CHECK-DAG: call {{.*}} @_Z4readIL_ZZ4testILi0EEivE4callEEiv()
// CHECK-DAG: call {{.*}} @_Z7addressIL_ZZ4testILi1EEivE4callEEPK4Callv()
// CHECK-DAG: call {{.*}} @_Z4readIL_ZZ4testILi1EEivE4callEEiv()
// CHECK-DAG: ret ptr @_ZZ4testILi0EEivE4call
// CHECK-DAG: ret ptr @_ZZ4testILi1EEivE4call
