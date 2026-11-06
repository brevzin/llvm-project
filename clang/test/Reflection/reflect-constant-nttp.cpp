// RUN: %clang_cc1 -std=c++2d -freflection -verify %s

// P4340 ext: identity-based conversion of constant template parameters
// through 'operator template' (stage 3: top-level
// semantics only; subobject recursion is tested separately).

namespace std::meta {
  using info = decltype(^^::);
}

consteval int gcd(int a, int b) {
  while (b) { int t = a % b; a = b; b = t; }
  return a;
}

struct Frac {
  int numer;
  int denom;
  consteval operator template() const;
};

template <int N, int D> constexpr Frac interned = Frac{N, D};

consteval Frac::operator template() const {
  int g = gcd(numer, denom);
  int n = numer / g, d = denom / g;
  // A minimal interning scheme (a real one would use substitute()).
  if (n == 1 && d == 2) return ^^interned<1, 2>;
  if (n == 3 && d == 4) return ^^interned<3, 4>;
  return ^^interned<0, 1>;
}

template <Frac F> constexpr int f() { return F.numer * 100 + F.denom; }
template <Frac F> constexpr const Frac* addr = &F;

// Value-equal arguments normalize to the same specialization...
static_assert(f<Frac{2, 4}> == f<Frac{1, 2}>);
// ...and distinct values stay distinct.
static_assert(f<Frac{2, 4}> != f<Frac{3, 4}>);

// The parameter binds to the returned object itself: address identity.
static_assert(addr<Frac{2, 4}> == &interned<1, 2>);
static_assert(addr<Frac{2, 4}> == addr<Frac{1, 2}>);

// The parameter carries the normalized value.
static_assert(f<Frac{2, 4}>() == 102);

// The customization runs on every argument, including one that names an
// interned object directly: interned<2,4>'s value {2,4} renormalizes.
static_assert(addr<interned<2, 4>> == &interned<1, 2>);

// ==== Private customization point ====

// The customization point may be private: the language invokes it without
// checking access.
class PrivFrac {
  int numer, denom;
  consteval operator template() const; // #privfrac-cp
public:
  consteval PrivFrac(int n, int d) : numer(n), denom(d) {}
  consteval int value() const { return numer * 100 + denom; }
};

template <int N, int D> constexpr PrivFrac priv_interned = PrivFrac{N, D};

consteval PrivFrac::operator template() const {
  int g = gcd(numer, denom);
  int n = numer / g, d = denom / g;
  if (n == 1 && d == 2) return ^^priv_interned<1, 2>;
  return ^^priv_interned<0, 1>;
}

template <PrivFrac F> constexpr const PrivFrac* priv_addr = &F;
static_assert(priv_addr<PrivFrac{2, 4}> == &priv_interned<1, 2>);
static_assert(priv_addr<PrivFrac{2, 4}>->value() == 102);

// A program can't call it at all (private or not): the language does, and
// std::meta::reflect_constant does.
consteval std::meta::info call_private(PrivFrac f) {
  return f.operator template(); // expected-error {{'operator template' can be named only in its own declaration (use 'std::meta::reflect_constant' to invoke it)}}
                               // expected-error@-1 {{'operator template' is a private member of 'PrivFrac'}}
                               // expected-note@#privfrac-cp {{implicitly declared private here}}
}

// ==== Rejected results ====

struct BadKind {
  int i;
  consteval operator template() const {
    return ^^int; // a type, not an object
  }
};
template <BadKind B> void g(); // expected-note {{returned a reflection of something other than an object or variable}}
void test_bad_kind() { g<BadKind{1}>(); } // expected-error {{no matching function}}

struct WrongType {
  int i;
  consteval operator template() const;
};
constexpr int not_a_wrongtype = 0;
consteval WrongType::operator template() const {
  return ^^not_a_wrongtype; // object of type int, not WrongType const
}
template <WrongType W> void h(); // expected-note {{an object of type 'const int' rather than 'const WrongType'}}
void test_wrong_type() { h<WrongType{1}>(); } // expected-error {{no matching function}}

struct NoLinkage {
  int i;
  consteval operator template() const;
};
// A local static has static storage duration and is usable in constant
// expressions, but has no linkage.
consteval NoLinkage::operator template() const {
  static constexpr NoLinkage local{7};
  return ^^local;
}
template <NoLinkage N> void k(); // expected-note {{an object with no linkage}}
void test_no_linkage() { k<NoLinkage{1}>(); } // expected-error {{no matching function}}

struct NotIdempotent {
  int i;
  consteval operator template() const;
};
template <int N> constexpr NotIdempotent ni = NotIdempotent{N};
consteval NotIdempotent::operator template() const {
  // ni<100> has value {100}, which renormalizes to ni<200>: not idempotent.
  return i < 100 ? ^^ni<100> : ^^ni<200>;
}
template <NotIdempotent N> void m(); // expected-note {{is not idempotent}}
void test_not_idempotent() { m<NotIdempotent{5}>(); } // expected-error {{no matching function}}

// ==== Deleted opt-out ====

struct Deleted { // expected-note {{'Deleted' is not a structural type because its 'operator template' is deleted}}
  int i;
  consteval operator template() const = delete;
};
template <Deleted D> void n(); // expected-error {{type 'Deleted' of non-type template parameter is not a structural type}}
