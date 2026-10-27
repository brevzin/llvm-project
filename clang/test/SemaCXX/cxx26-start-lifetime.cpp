// RUN: %clang_cc1 -std=c++26 -fsyntax-only -verify=expected,tree %s
// RUN: %clang_cc1 -std=c++26 -fsyntax-only -verify=expected,bytecode %s -fexperimental-new-constant-interpreter
// RUN: %clang_cc1 -std=c++23 -fsyntax-only -verify=cxx23 -DCXX23 %s

// P3726R2: std::start_lifetime (__builtin_start_lifetime), inactive union
// elemental subobjects in constituent values, and (C++26 [class.union]p2) a
// union whose active member's lifetime has ended has no active member.

#ifdef CXX23
// cxx23-no-diagnostics
static_assert(!__has_builtin(__builtin_start_lifetime));
#else
static_assert(__has_builtin(__builtin_start_lifetime));

using size_t = decltype(sizeof 0);
inline constexpr void *operator new(size_t, void *p) noexcept { return p; }
template <class T> constexpr void destroy_at(T *p) { p->~T(); }

namespace std {
template <class T> constexpr void start_lifetime(T &r) noexcept {
  __builtin_start_lifetime(__builtin_addressof(r));
}
} // namespace std

struct S {
  int v;
  constexpr S(int v) : v(v) {}
  constexpr ~S() {}
};

namespace mandates {
struct NotAggregate { NotAggregate(); };
struct UserDtor { int x; ~UserDtor(); };
struct Incomplete; // expected-note {{forward declaration of 'mandates::Incomplete'}}
void f(NotAggregate *a, UserDtor *b, Incomplete *c, const S (*d)[2],
       volatile int (*e)[2], int i, void *v, S (*ok)[2], S (*ok2)[2][3]) {
  __builtin_start_lifetime(a); // expected-error {{argument to '__builtin_start_lifetime' must be a pointer to an aggregate type; 'NotAggregate' is not}}
  __builtin_start_lifetime(b); // expected-error {{argument to '__builtin_start_lifetime' must be a pointer to an implicit-lifetime type; 'UserDtor' is not}}
  __builtin_start_lifetime(c); // expected-error {{incomplete type 'Incomplete' where a complete type is required}}
  __builtin_start_lifetime(d); // expected-error {{argument to '__builtin_start_lifetime' must be a pointer to a type that is not const or volatile; 'const S[2]' is not}}
  __builtin_start_lifetime(e); // expected-error {{argument to '__builtin_start_lifetime' must be a pointer to a type that is not const or volatile; 'volatile int[2]' is not}}
  __builtin_start_lifetime(i); // expected-error {{argument to '__builtin_start_lifetime' must be a pointer to an object type; 'int' is not}}
  __builtin_start_lifetime(v); // expected-error {{argument to '__builtin_start_lifetime' must be a pointer to an object type; 'void' is not}}
  __builtin_start_lifetime();  // expected-error {{too few arguments to function call}}
  __builtin_start_lifetime(ok);
  __builtin_start_lifetime(ok2);
  // A runtime call (the builtin is constexpr, not consteval): a no-op.
}
template <class T> void g(T *p) {
  __builtin_start_lifetime(p); // expected-error {{must be a pointer to an aggregate type; 'mandates::NotAggregate' is not}}
}
template void g(S (*)[2]);
template void g(NotAggregate *); // expected-note {{in instantiation of}}
} // namespace mandates

// P3726R2's wording example.
namespace wording {
struct A {
  struct X {
    int i;
    int j; // expected-note {{subobject declared here}}
  };
  struct Y {
    X x1;
    X x2;
  };
  union {
    int i;
    int arr[4];
    Y y;
  };
};
// One departure from the paper's example: 'constexpr A v1;' is ill-formed,
// since A is not const-default-constructible ([dcl.init.general]p7: no member
// of its anonymous union has a default member initializer).
constexpr A v1 = [] { A a; return a; }(); // ok, no constituent values
constexpr A v2{.i=1}; // ok, the constituent values are {v2.i}
constexpr A v3 = []{
  A a;
  std::start_lifetime(a.arr); // ok, arr is now the active element of the union
  new (&a.arr[1]) int(1);
  a.arr[2] = 2;
  return a;
}();                 // ok, the constituent values are {v3.arr[1], v3.arr[2]}
static_assert(v3.arr[1] == 1 && v3.arr[2] == 2);
constexpr A v4 = []{ // expected-error {{constexpr variable 'v4' must be initialized by a constant expression}} \
                     // tree-note {{in call to}} \
                     // bytecode-note {{subobject 'j' is not initialized}}
  A a;
  a.y.x1.i = 1;
  a.y.x2.j = 2;
  return a; // tree-note {{subobject 'j' is not initialized}} tree-note {{in call to 'A(a)'}}
}();                 // error: the constituent values include v4.y.x1.j and v4.y.x2.i
} // namespace wording

namespace fixed_vector {
template <class T, size_t N> struct FixedVector {
  union { T storage[N]; };
  size_t size = 0;
  constexpr FixedVector() { std::start_lifetime(storage); }
  // Push `pushes`, pop `pops`, then end the lifetime of element `hole`.
  constexpr FixedVector(int pushes, int pops, int hole) : FixedVector() {
    for (int i = 0; i < pushes; ++i)
      push_back(i + 1);
    for (int i = 0; i < pops; ++i)
      pop_back();
    if (hole >= 0)
      destroy_at(&storage[hole]);
  }
  constexpr void push_back(int v) { ::new (&storage[size++]) T(v); }
  constexpr void pop_back() { destroy_at(&storage[--size]); }
  constexpr ~FixedVector() {}
};

// Holes at the end, at the front, and in the middle.
constexpr FixedVector<S, 4> end(3, 1, -1);
constexpr FixedVector<S, 4> front(3, 0, 0);
constexpr FixedVector<S, 4> middle(3, 0, 1);
static_assert(end.size == 2 && front.storage[1].v == 2 &&
              middle.storage[2].v == 3);

// Template-argument equivalence: out-of-lifetime elements are equivalent.
template <FixedVector<int, 4> V> struct X {};
static_assert(__is_same(X<FixedVector<int, 4>(3, 1, -1)>,
                        X<FixedVector<int, 4>(3, 1, -1)>));
static_assert(!__is_same(X<FixedVector<int, 4>(3, 0, 0)>,
                         X<FixedVector<int, 4>(3, 0, 1)>));
// Pushing three and popping one is the same value as pushing two.
static_assert(__is_same(X<FixedVector<int, 4>(2, 0, -1)>,
                        X<FixedVector<int, 4>(3, 1, -1)>));
static_assert(!__is_same(X<FixedVector<int, 4>(2, 0, -1)>,
                         X<FixedVector<int, 4>(3, 0, -1)>));

// A trivial copy copies the holes.
constexpr int copy() {
  FixedVector<int, 4> v(2, 1, -1);
  FixedVector<int, 4> w = v;
  return w.storage[0] + int(w.size);
}
static_assert(copy() == 2);

// Elements outside their lifetimes can't be read...
constexpr int read_hole() {
  FixedVector<S, 4> v(2, 0, 0); // expected-note {{declared here}}
  return v.storage[0].v; // expected-note {{read of object outside its lifetime}}
}
static_assert(read_hole()); // expected-error {{not an integral constant expression}} \
                            // expected-note {{in call to 'read_hole()'}}
// ...but an indeterminate element is within its lifetime: not skipped.
struct Ind {
  union { int a[2]; }; // expected-note {{subobject declared here}}
  constexpr Ind() {
    std::start_lifetime(a);
    ::new (&a[0]) int;
  }
};
constexpr Ind ind; // expected-error {{must be initialized by a constant expression}} \
                   // expected-note {{subobject 'a' is not initialized}}

// Ending the lifetime of elements at any index (here, all of them) leaves the
// array active.
constexpr int all_popped() {
  FixedVector<S, 64> v;
  for (int i = 0; i != 64; ++i)
    v.push_back(i);
  while (v.size)
    v.pop_back();
  v.push_back(42);
  return v.storage[0].v;
}
static_assert(all_popped() == 42);

// Starting the lifetime again changes nothing.
constexpr int again() {
  FixedVector<S, 4> v(1, 0, -1);
  std::start_lifetime(v.storage);
  std::start_lifetime(v.storage);
  return v.storage[0].v;
}
static_assert(again() == 1);
} // namespace fixed_vector

// Assignment implicitly creates an out-of-lifetime element of an array that
// is a union elemental subobject, as for a union member (P3726R2's intent;
// [class.union]p5 doesn't say so yet). An array created this way, element or
// union member, has no elements within their lifetimes but the one assigned.
namespace assignment {
struct V {
  union { int s[4]; };
  size_t n = 0;
  constexpr V() { std::start_lifetime(s); }
};
constexpr V push() {
  V v;
  v.s[v.n++] = 5;
  v.s[v.n++] = 6;
  return v;
}
constexpr V pushed = push();
static_assert(pushed.s[1] == 6 && pushed.n == 2);

struct M {
  union { int s[2][3]; };
  constexpr M() { std::start_lifetime(s); }
};
constexpr M row = [] { M m; m.s[1][2] = 7; return m; }();
static_assert(row.s[1][2] == 7);

union U { int a[3]; long l; };
constexpr U u = [] { U u; u.l = 1; u.a[1] = 4; return u; }();
static_assert(u.a[1] == 4);
constexpr int read_hole() {
  U u; // expected-note {{declared here}}
  u.a[1] = 4;
  return u.a[0]; // expected-note {{read of object outside its lifetime}}
}
static_assert(read_hole()); // expected-error {{not an integral constant expression}} \
                            // expected-note {{in call to 'read_hole()'}}

// Not for an element of a class type with a non-trivial default constructor.
struct NT {
  int v;
  constexpr NT() : v(1) {}
  constexpr NT &operator=(int x) { v = x; return *this; }
};
struct W {
  union { NT s[2]; };
  constexpr W() { std::start_lifetime(s); }
};
constexpr int non_trivial() {
  W w; // expected-note {{declared here}}
  w.s[0] = 3; // expected-note {{member call on object outside its lifetime}}
  return w.s[0].v;
}
static_assert(non_trivial()); // expected-error {{not an integral constant expression}} \
                              // expected-note {{in call to 'non_trivial()'}}
} // namespace assignment

// A class aggregate: its members don't begin their lifetimes.
namespace aggregate {
struct Agg { int a; int b; };
union U {
  Agg g;
  long l;
  constexpr U() {}
};
constexpr int ok() {
  U u;
  std::start_lifetime(u.g);
  ::new (&u.g.b) int(2);
  return u.g.b;
}
static_assert(ok() == 2);
constexpr int bad() {
  U u; // expected-note {{declared here}}
  std::start_lifetime(u.g);
  return u.g.a; // tree-note {{read of object outside its lifetime}} \
                // bytecode-note {{read of uninitialized object}}
}
static_assert(bad()); // expected-error {{not an integral constant expression}} \
                      // expected-note {{in call to 'bad()'}}
} // namespace aggregate

// Only the top-level array of a multidimensional array starts.
namespace multidim {
struct M {
  union { int s[2][3]; };
  constexpr M() { std::start_lifetime(s); }
};
constexpr int rows() {
  M m; // expected-note {{declared here}}
  ::new (&m.s[0][0]) int(1); // expected-note {{construction of subobject of object outside its lifetime}}
  return m.s[0][0];
}
static_assert(rows()); // expected-error {{not an integral constant expression}} \
                       // expected-note {{in call to 'rows()'}}
constexpr M one_row = [] {
  M m;
  std::start_lifetime(m.s[0]);
  ::new (&m.s[0][1]) int(5);
  return m;
}();
static_assert(one_row.s[0][1] == 5);
} // namespace multidim

// Ending the lifetime of the active member leaves no active member.
namespace deactivate {
union U {
  int a;
  S s;
  constexpr U() {}
  constexpr ~U() {}
};
template <U V> struct X {};
constexpr U empty() { return U(); }
constexpr U destroyed() {
  U u;
  ::new (&u.s) S(1);
  destroy_at(&u.s);
  return u;
}
constexpr U destroyed_scalar() {
  U u;
  u.a = 1;
  destroy_at(&u.a);
  return u;
}
static_assert(__is_same(X<destroyed()>, X<empty()>));
static_assert(__is_same(X<destroyed_scalar()>, X<empty()>));
constexpr int read() {
  U u;
  ::new (&u.s) S(1);
  destroy_at(&u.s);
  return u.s.v; // expected-note {{read of member 's' of union with no active member}}
}
static_assert(read()); // expected-error {{not an integral constant expression}} \
                       // expected-note {{in call to 'read()'}}
consteval bool within() {
  U u;
  ::new (&u.s) S(1);
  destroy_at(&u.s);
  return __builtin_is_within_lifetime(&u.s);
}
static_assert(!within());
constexpr int other() {
  U u;
  ::new (&u.s) S(1);
  destroy_at(&u.s);
  u.a = 5; // another member becomes active
  return u.a;
}
static_assert(other() == 5);
} // namespace deactivate

namespace errors {
constexpr bool null() {
  int (*p)[2] = nullptr;
  __builtin_start_lifetime(p); // expected-note {{'__builtin_start_lifetime' cannot be called with a null pointer}}
  return true;
}
static_assert(null()); // expected-error {{not an integral constant expression}} \
                       // expected-note {{in call to 'null()'}}
constexpr bool past_end() {
  int a[1][2]; // expected-note {{array 'a' declared here}}
  std::start_lifetime(a[1]); // expected-note {{'std::start_lifetime' cannot be called with a one-past-the-end pointer}} \
                             // expected-note {{in call to 'start_lifetime<int[2]>(a[1])'}} \
                             // expected-warning {{array index 1 is past the end of the array}}
  return true;
}
static_assert(past_end()); // expected-error {{not an integral constant expression}} \
                           // expected-note {{in call to 'past_end()'}}
} // namespace errors

// At runtime, from a constexpr function not constant-evaluated.
constexpr void at_runtime(S (*p)[2]) { __builtin_start_lifetime(p); }
void call_at_runtime(S (*p)[2]) { at_runtime(p); }
#endif
