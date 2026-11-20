// RUN: %clang_cc1 -std=c++2d -verify -fsyntax-only %s

// expected-no-diagnostics

// A do-expression body outside a function gets a made-up context to declare
// things in, parented in the namespace (see Sema::ActOnStartDoExpr). In a
// template -- a default argument of a function template, the initializer of
// a variable template, an alias template, a concept -- the body belongs to the
// template, but the namespace says nothing of that. The context has to say it
// itself: otherwise a local class in the body was taken to be non-dependent,
// had its implicit members declared up front, and asserted when instantiated
// ("substituting function without type source info"), or used the template
// parameter in a class that was never instantiated.

namespace variable_template {
  template <class T> constexpr T v1 = do { struct L { T v; }; do return L{T(3)}.v; };
  static_assert(v1<int> == 3);
  static_assert(v1<double> == 3.0);

  template <class T> constexpr int v2 = do {
    struct L { constexpr int f() const { return sizeof(T); } };
    do return L{}.f();
  };
  static_assert(v2<char> == 1);
  static_assert(v2<double> == 8);

  // A local class with a non-trivial destructor, which used to be declared
  // eagerly in the pattern.
  template <class T> constexpr int v3 = do {
    struct L { int v = sizeof(T); constexpr ~L() {} };
    do return L{}.v;
  };
  static_assert(v3<short> == 2);
}

namespace function_template_default_argument {
  template <class T>
  constexpr int f(int p = do { struct L { T v; }; do return int(sizeof(L{}.v)); }) {
    return p;
  }
  static_assert(f<char>() == 1);
  static_assert(f<long long>() == 8);
}

namespace concept_definition {
  template <class T> concept Wide = do {
    struct L { int v = sizeof(T); };
    do return L{}.v > 1;
  };
  static_assert(Wide<int> && !Wide<char>);
}

namespace alias_template {
  template <class T> using Arr = int[do { struct L { int v = sizeof(T); }; do return L{}.v; }];
  static_assert(sizeof(Arr<char>) == sizeof(int));
  static_assert(sizeof(Arr<double>) == 8 * sizeof(int));
}

namespace static_data_member {
  template <class T> struct X { static const int c; };
  template <class T> const int X<T>::c = do { struct L { T v; }; do return int(sizeof(L)); };
  static_assert(X<int>::c == 4);
}
