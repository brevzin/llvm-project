// RUN: %clang_cc1 -std=c++26 -freflection -verify -verify-ignore-unexpected=note %s

// Test for P3603R0: Allowing consteval variables

// Global consteval variables
consteval int x = 42;
static_assert(x == 42);

consteval int f(int i) { return x + i; }
consteval int g(int) { return 0; }
constexpr int h(int) { return 0; }

constexpr auto pf1 = f; // expected-error {{constant expression}}
consteval auto pf2 = f; // OK

struct C { int(*p)(int); };
constexpr auto c1 = C{.p=f}; // expected-error {{constant expression}}
consteval auto c2 = C{.p=f}; // ok

int main(int argc, char**) {
    // Local consteval variables
    consteval int y = 0;
    static_assert(y == 0);

    // Error: consteval variables must be initialized with constant expressions
    consteval int z = argc; // expected-error {{constexpr variable 'z' must be initialized by a constant expression}}

    // Error: expressions involving consteval variables must be constant-evaluated
    int a1 = y + argc; // expected-error {{expressions involving consteval variables are only allowed in constant-evaluated contexts}}
    int a2 = y + x;    // expected-error 2 {{expressions involving consteval variables}}

    // OK: consteval functions create constant-evaluated contexts
    int a3 = g(y);
    int a4 = g(x);

    // Error: constexpr functions don't create constant-evaluated contexts
    int a5 = h(y);    // expected-error {{expressions involving consteval variables}}

    // OK: static_assert creates a constant-evaluated context
    static_assert(f(1) == 43);

    // OK: consteval variables can be captured and used in constant expressions
    auto f2 = [](int i) { return x + i; };
    static_assert(f2(1) == 43);
}
