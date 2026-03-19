// RUN: %clang_cc1 -std=c++26 -freflection -fexpansion-statements -verify -verify-ignore-unexpected=note %s

// Test for P3603R0: Allowing consteval variables

using info = decltype(^^::);

// Global consteval variables
consteval int x = 42;
static_assert(x == 42);
using I = decltype(x);

consteval auto lvalue_to_rvalue(auto x) { return x; }

namespace ptr_ref {
    constexpr int const* p1 = &x; // expected-error {{constant-evaluated context}}
    consteval int const* p2 = &x;
    constexpr int const& r1 = x;  // expected-error {{constant-evaluated context}}
    consteval int const& r2 = x;

    template <class T> struct Wrap { T t; };
    constexpr auto w1 = Wrap<int const*>{&x}; // expected-error {{constant-evaluated context}}
    constexpr auto w2 = Wrap<int const&>{x};  // expected-error {{constant-evaluated context}}
    consteval auto w3 = Wrap<int const*>{&x};
    consteval auto w4 = Wrap<int const&>{x};
}

consteval int f(int i) { return x + i; }
consteval int g(int) { return 0; }
constexpr int h(int const&) { return 0; }

constexpr auto pf1 = f; // expected-error {{constant expression}}
consteval auto pf2 = f; // OK

struct Wrap { int(*p)(int); };
constexpr auto c1 = Wrap{.p=f}; // expected-error {{constant expression}}
consteval auto c2 = Wrap{.p=f}; // ok

void local(int argc) {
    // Local consteval variables
    consteval int y = 0;
    static_assert(y == 0);

    // Error: consteval variables must be initialized with constant expressions
    consteval int z = argc; // expected-error {{constexpr variable 'z' must be initialized by a constant expression}}

    // Error: expressions involving consteval variables must be constant-evaluated
    int a1 = y + argc;
    int a2 = y + x;

    int a1b = lvalue_to_rvalue(y) + argc;
    int a2b = lvalue_to_rvalue(y) + lvalue_to_rvalue(y);

    // OK: consteval functions create constant-evaluated contexts
    int a3 = g(y);
    int a4 = g(x);

    // Error: constexpr functions don't create constant-evaluated contexts
    int a5 = h(y);     // expected-error {{expressions involving consteval-only values}}

    int const& a6 = y; // expected-error {{constant-evaluated context}}
    constexpr int const& a7 = y; // expected-error {{constant expression}}

    // OK: static_assert creates a constant-evaluated context
    static_assert(f(1) == 43);

    // OK: consteval variables can be captured and used in constant expressions
    auto f2 = [](int i) { return x + i; };
    static_assert(f2(1) == 43);
}

void expansion() {
    struct Ptr { int const* p; };
    static consteval int i = 0;

    struct array {
    private:
        int elems[3];
    public:
        constexpr array(int x, int y, int z) : elems{x, y, z} { }
        constexpr const int* begin() const { return elems; }
        constexpr const int* end() const { return elems + 3; }
    };

    template for (consteval int x : {1, 2, 3}) {
        static_assert(x < 10);
        constexpr int const* p = &x; // expected-error 3 {{constant expression}}
    }

    template for (consteval auto p : Ptr{&i}) {

    }

    template for (consteval int e : array{1, 2, 3}) {
        static_assert(e < 10);
        constexpr int const* p = &e; // expected-error 3 {{constant expression}}
    }
}

namespace N1 {
    template <auto F>
    struct D {
        static int call1(int i) {
            return F(i); // expected-error {{consteval}}
        }

        static constexpr int call2(int i) {
            return F(i);
        }

        static consteval int call3(int i) {
            return F(i); // OK
        }
    };

    int i = D<f>::call1(3);
    static_assert(D<f>::call2(5) == 47); // ok
    static_assert(D<f>::call3(5) == 47); // ok

    constexpr auto call2a = D<f>::call2; // expected-error {{constant expression}}
    consteval auto call2b = D<f>::call2; // ok
}

namespace N2 {
    template <auto V>
    struct C {
        static constexpr auto value = V;
    };

    template <auto V>
    static constexpr auto var = V;

    constexpr auto i = C<0>::value;
    static_assert(i == 0);
    constexpr auto pf1 = C<::f>::value; // expected-error {{constant expression}}
    consteval auto pf2 = C<::f>::value;
    static_assert(pf2(2) == 44);
    constexpr auto pf3 = var<::f>; // expected-error {{constant expression}}
    consteval auto pf4 = var<::f>;
    static_assert(pf4(2) == 44);

    constexpr auto w1 = C<Wrap{.p=::f}>::value; // expected-error {{constant expression}}
    consteval auto w2 = C<Wrap{.p=::f}>::value;
    static_assert(w2.p(2) == 44);
    constexpr auto w3 = var<Wrap{.p=::f}>; // expected-error {{constant expression}}
    consteval auto w4 = var<Wrap{.p=::f}>;
    static_assert(w4.p(2) == 44);

}

namespace N3 {
    // the not_fn example from
    // https://stackoverflow.com/questions/79763246/cannot-use-stdnot-fn-with-immediate-functions/79763257#79763257

    template <class F>
    constexpr auto not_fn(F f) {
        return [f](auto... xs){
            return not f(xs...);
        };
    }

    template <auto F>
    constexpr auto not_fn() {
        return [](auto... xs) {
            return not F(xs...);
        };
    }

    consteval bool f() { return false; }
    static_assert( not_fn(f)() );   // OK
    static_assert( not_fn<f>()() ); // OK
}

namespace N4 {
    constexpr info r1 = ^^int; //expected-error {{constant-evaluated context}}
    consteval info r2 = ^^int; // OK

    constexpr int size_of1(info ) { return 0; }
    consteval int size_of2(info ) { return 0; }

    void test() {
        int v1 = size_of1(r2); // expected-error {{constant-evaluated}}
        int v2 = size_of2(r2);
        int v3 = size_of1(^^int); // expected-error {{constant-evaluated}}
        int v4 = size_of2(^^int);
    }


    struct M { info r; };
    constexpr auto m1 = M{.r=^^int}; // expected-error {{constant-evaluated context}}
    consteval auto m2 = M{.r=^^int}; // OK
    constexpr auto m3 = m2;          // expected-error {{constant-evaluated context}}
    constexpr auto m4 = []{ return m2; }(); // expected-error {{constant-evaluated context}}
    consteval auto m5 = []{ return m2; }(); // OK

    constexpr info* pr = nullptr; // OK
    constexpr M const* pm1 = &m2; // expected-error {{constant-evaluated context}}
    consteval M const* pm2 = &m2; // OK
}

namespace N5 {
    void runtime(int ) { }
    consteval int consteval_id(int i) { return i; }

    void expansion_statement_interaction(int var) {
        template for (int _ : {1, 2}) {
            consteval int y = 10;
            runtime(consteval_id(y));
            int sum = y + var; // FIXME: Should this error? Currently allowed due to lvalue-to-rvalue conversion
        }
    }
}

void lambda_capture() {
    constexpr int x = 1;
    consteval int y = 2;
    auto fx = []{ return x; };
    auto fy = []{ return y; };
}

namespace N6 {
    struct S {
        int i;
        info r;
    };
    consteval S s = {.r = ^^int};

    consteval const int &r1 = s.i; // ok
    constexpr const int &r2 = s.i; // expected-error {{constant-evaluated context}}
    const int &r3 = s.i; // expected-error {{constant-evaluated context}}
    int x = s.i; // ok (because constant expression)

    auto local() -> void {
        consteval const int &lr1 = s.i; // ok
        constexpr const int &lr2 = s.i; // expected-error {{constant-evaluated context}}
        const int &lr3 = s.i; // expected-error {{constant-evaluated context}}
        int lx = s.i; // ok (because constant expression)
    }
}

namespace N7 {
    struct S {
        info r;
        template <class> constexpr auto eq() const -> bool { return this->r == info(); }
        template <class> constexpr auto ne() const -> bool { return this->r != info(); }
        template <class> constexpr auto id() const -> info { return this-> r; }
    };

    constexpr auto p1 = &S::eq<int>; // expected-error {{constant expression}}
    consteval auto p2 = &S::eq<int>; // ok
    constexpr auto p3 = &S::ne<int>; // expected-error {{constant expression}}
    consteval auto p4 = &S::ne<int>; // ok
    constexpr auto p5 = &S::id<int>; // ok (doesn't escalate)

    constexpr auto const& r1 = S{^^int}; // expected-error {{constant-evaluated context}}
    consteval auto const& r2 = S{^^int}; // ok
}

namespace N8 {
    info var; // expected-error {{constant}}
    auto normal_func() -> void {
        var = ^^int; // expected-error {{constant}}
    }
    constexpr auto constexpr_func() -> bool {
        var = ^^int; // expected-error {{constant}}
        return true;
    }
    consteval auto consteval_func() -> bool {
        var = ^^int;
        return true;
    }
    static_assert(consteval_func()); // expected-error {{constant}}
}

namespace N9 {
    struct S { info r; };
    union U { info r; int i; };

    info a;       // expected-error {{constant}}
    S b;          // expected-error {{constant}}
    U c{.i = 1};  // ok
    U d{.r={}};   // expected-error {{constant}}
    auto normal() -> void {
        info e;      // expected-error {{constant}}
        S f;         // expected-error {{constant}}
        U g{.i = 2}; // ok
        U h{.r={}};  // expected-error {{constant}}
    }
}
