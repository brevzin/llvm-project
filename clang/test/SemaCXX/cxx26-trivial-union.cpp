// RUN: %clang_cc1 -std=c++23 -fsyntax-only -verify=cxx23 %s
// RUN: %clang_cc1 -std=c++26 -fsyntax-only -verify=cxx26 %s
// RUN: %clang_cc1 -std=c++26 -fsyntax-only -verify=cxx26 %s -fexperimental-new-constant-interpreter

// P3074R7 (trivial unions), with the destructor rule of CWG3189:
// - a union's variant members don't make its default constructor or
//   destructor non-trivial;
// - a variant member's default constructor is never called by default
//   construction, and the "all members const" rule is gone;
// - a variant member whose destructor is deleted, inaccessible, or
//   non-trivial deletes the destructor of X only if it has a default member
//   initializer or X has a user-provided constructor. Members of anonymous
//   unions are variant members of the enclosing class.

// cxx23-no-diagnostics

#define TRIVIAL(T)                                                             \
  (__is_trivially_constructible(T) && __is_trivially_destructible(T))

struct NT {
  NT();
  NT(const NT &);
  ~NT();
};

#if __cplusplus > 202302L
#define CXX26 1
#else
#define CXX26 0
#endif

// P3074R7 section 4.
union U1 { NT s; };
static_assert(TRIVIAL(U1) == CXX26);
static_assert(__is_constructible(U1) == CXX26);
static_assert(__is_destructible(U1) == CXX26);

union U2 { NT s = NT(); };
U2 *make_u2() { return new U2; } // the default constructor is not deleted
static_assert(!__is_trivially_constructible(U2));
static_assert(!__is_destructible(U2));

union U3 { NT s[10]; };
static_assert(TRIVIAL(U3) == CXX26);

union U4 { NT s; U4 *next = nullptr; };
static_assert(!__is_trivially_constructible(U4));
static_assert(__is_constructible(U4) == CXX26); // (needs the destructor)
static_assert(__is_trivially_destructible(U4) == CXX26);

// CWG3189's examples: well-formed in both modes.
union C1 {
  int a = 42;
  long b;
} c1;
union C2 {
  C2(int i) : i(i) {}
  int i;
} c2(5);
// A default member initializer still makes the default constructor
// non-trivial.
static_assert(!__is_trivially_constructible(C1));

// A user-provided constructor -- any constructor -- deletes the destructor.
union D1 { D1(int); NT s; };
static_assert(!__is_destructible(D1));
union D3 { template <class T> D3(T); NT s; };
static_assert(!__is_destructible(D3));
#if CXX26
union D2 { D2(); NT s; };
D2::D2() = default; // user-provided: not defaulted on its first declaration
static_assert(!__is_destructible(D2));
// Explicitly defaulted on its first declaration: not user-provided, and the
// explicitly defaulted members are trivial as the implicit ones would be.
union D4 { D4() = default; ~D4() = default; NT s; };
static_assert(TRIVIAL(D4));
#endif

// In a class template, per specialization.
template <class T> union TU { TU(int); T s; };
static_assert(!__is_destructible(TU<NT>) || !CXX26);
static_assert(__is_destructible(TU<int>));
static_assert(TRIVIAL(TU<NT>) == false); // (user-provided constructor)

// Members of anonymous unions are variant members of the enclosing class.
struct E1 { E1(int); union { NT s; }; };
static_assert(!__is_destructible(E1));
struct E2 { union { NT s; }; };
static_assert(TRIVIAL(E2) == CXX26);
// An inherited constructor is not user-provided in the derived class.
struct B { B(int); };
struct E3 : B { using B::B; union { NT s; }; };
static_assert(__is_trivially_destructible(E3) == CXX26);

// Nested anonymous members, without and with a default member initializer.
union N1 { struct { NT x; }; };
static_assert(TRIVIAL(N1) == CXX26);
union N1d { struct { NT x = NT(); }; };
static_assert(!__is_destructible(N1d));
struct N2 { union { NT x; }; };
static_assert(TRIVIAL(N2) == CXX26);
struct N2d { union { NT x = NT(); }; };
static_assert(!__is_destructible(N2d));
union N3 { union { NT x; }; };
static_assert(TRIVIAL(N3) == CXX26);
union N3d { union { NT x = NT(); }; };
static_assert(!__is_destructible(N3d));
struct N4 { union { struct { union { NT x; }; }; }; };
static_assert(TRIVIAL(N4) == CXX26);
struct N4d { union { struct { union { NT x = NT(); }; }; }; };
static_assert(!__is_destructible(N4d));
// A user-provided constructor reaches through nested anonymous unions.
struct N5 { N5(int); union { union { NT x; }; }; };
static_assert(!__is_destructible(N5) || !CXX26);

// No "all variant members const" rule.
union K1 { const int a; const int b; };
static_assert(__is_constructible(K1) == CXX26);
struct K2 { union { const int a; const int b; }; };
static_assert(__is_constructible(K2) == CXX26);

// Arrays, including multidimensional ones.
union A1 { NT s[2][3]; };
static_assert(TRIVIAL(A1) == CXX26);

// A variant member with a deleted destructor: the union's destructor is
// trivial unless a default member initializer or user-provided constructor
// would need it.
struct DD { ~DD() = delete; };
union V1 { DD d; };
static_assert(__is_destructible(V1) == CXX26);
union V2 { V2(); DD d; };
static_assert(!__is_destructible(V2));

// An inaccessible destructor with a default member initializer deletes both
// the destructor and the default constructor.
class PD {
  ~PD() {}
public:
  PD() {}
};
union V3 { PD p = {}; };
static_assert(!__is_destructible(V3));
static_assert(!__is_constructible(V3));

// Copy and move are unchanged.
union CP { NT s; };
static_assert(!__is_constructible(CP, const CP &));
static_assert(!__is_constructible(CP, CP &&));

#if CXX26
// Diagnostics.
union G1 {
  G1(int); // cxx26-note {{user-provided constructor declared here}}
  NT s;    // cxx26-note {{destructor of 'G1' is implicitly deleted because variant member 's' has a non-trivial destructor and 'G1' has a user-provided constructor}}
};
void g1() { G1 g(1); } // cxx26-error {{attempt to use a deleted function}}

union G2 {
  NT s = NT(); // cxx26-note {{destructor of 'G2' is implicitly deleted because variant member 's' has a non-trivial destructor and has a default member initializer}}
};
void g2() { G2 g; } // cxx26-error {{attempt to use a deleted function}}

// Default construction leaves no member active: reading one is not a
// constant expression.
struct CNT {
  int v;
  constexpr CNT() : v(1) {}
  constexpr ~CNT() {}
};
union R { CNT c; };
static_assert(TRIVIAL(R));
constexpr int read() {
  R r;
  return r.c.v; // cxx26-note {{read of member 'c' of union with no active member}}
}
static_assert(read() == 1); // cxx26-error {{static assertion expression is not an integral constant expression}} \
                            // cxx26-note {{in call to 'read()'}}
#endif
