// RUN: %clang_cc1 -std=c++26 -triple x86_64-linux-gnu -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -std=c++26 -triple x86_64-linux-gnu -emit-llvm -o - %s -fexperimental-new-constant-interpreter | FileCheck %s

// P3074R7/P3726R2: code generation and mangling for trivial unions,
// start_lifetime, and array elements not within their lifetimes.

using size_t = decltype(sizeof 0);
inline constexpr void *operator new(size_t, void *p) noexcept { return p; }

struct S {
  int v;
  constexpr S(int v) : v(v) {}
  constexpr ~S() {}
};

// The constant g1 below, with holes:
// CHECK: @_ZL2g1 = internal constant %struct.FV { %union.anon { [4 x i32] [i32 1, i32 undef, i32 3, i32 undef] }, i64 3 }

// __builtin_start_lifetime does nothing at runtime.
// CHECK-LABEL: define {{.*}}void @_Z2rtPA2_1S(
// CHECK-NOT: call
// CHECK: ret void
void rt(S (*p)[2]) { __builtin_start_lifetime(p); }

template <class T, size_t N> struct FV {
  union { T storage[N]; };
  size_t size = 0;
  constexpr FV() { __builtin_start_lifetime(&storage); }
  constexpr FV(int pushes, int pops, int hole) : FV() {
    for (int i = 0; i < pushes; ++i)
      ::new (&storage[size++]) T(i + 1);
    for (int i = 0; i < pops; ++i)
      storage[--size].~T();
    if (hole >= 0)
      storage[hole].~T();
  }
  constexpr ~FV() {}
};

// A constant with holes: the holes are undefined. (Checked at the top.)
constexpr FV<int, 4> g1(3, 0, 1);
const FV<int, 4> *use1() { return &g1; }

// Elements not within their lifetimes mangle as L<type>E. (The ABI doesn't
// specify this yet; this pins the current output.)
template <FV<int, 4> V> void nt() {}
// CHECK-LABEL: define {{.*}}@_Z7call_ntv(
// CHECK: call void @_Z2ntIXtl2FVIiLm4EEtlNS1_Ut_Edi7storagetlA4_iLi1ELiELi3ELiEEELm3EEEEvv()
// CHECK: call void @_Z2ntIXtl2FVIiLm4EEtlNS1_Ut_Edi7storagetlA4_iLi1ELi2ELiELiEEELm2EEEEvv()
void call_nt() {
  nt<FV<int, 4>(3, 0, 1)>();
  nt<FV<int, 4>(2, 0, -1)>();
}

// P3726R2 section 1: default construction starts no member's lifetime, so
// this mangles as it did before C++26.
union U { int a, b; };
constexpr U make() { U u; return u; }
template <U V> void ru() {}
// CHECK-LABEL: define {{.*}}@_Z7call_ruv(
// CHECK: call void @_Z2ruIL1UEEvv()
void call_ru() { ru<make()>(); }

// A union whose active member has been destroyed has no active member: the
// same template argument as a default-constructed one.
union U2 {
  int a;
  S s;
  constexpr U2() {}
  constexpr ~U2() {}
};
constexpr U2 destroyed() {
  U2 u;
  ::new (&u.s) S(1);
  u.s.~S();
  return u;
}
template <U2 V> void rd() {}
// CHECK-LABEL: define {{.*}}@_Z7call_rdv(
// CHECK: call void @_Z2rdIL2U2EEvv()
// CHECK-NEXT: call void @_Z2rdIL2U2EEvv()
void call_rd() {
  rd<destroyed()>();
  rd<U2{}>();
}
