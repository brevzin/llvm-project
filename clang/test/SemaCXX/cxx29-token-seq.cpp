// RUN: %clang_cc1 -std=c++26 -freflection -fexpansion-statements -verify -verify-ignore-unexpected=note %s

using info = decltype(^^::);
using size_t = decltype(sizeof(0));
struct string_view {
private:
    char const* p_;
    size_t len_;

public:
    constexpr string_view(char const* p) : p_(p), len_(__builtin_strlen(p)) { }
    constexpr auto data() const -> char const* { return p_; }
    constexpr auto size() const -> size_t { return len_; }
};

namespace N1 {
    constexpr info tok = ^^{ constexpr int x = 42; };
    static_assert(x == 42); // expected-error {{use of undeclared}}
    consteval {
        __builtin_inject(tok);
    }
    static_assert(x == 42);
}

namespace N2 {
    constexpr int value = 5;
    constexpr info tok = ^^{ constexpr int y = \(value); };
    static_assert(y == value); // expected-error {{use of undeclared}}
    consteval {
        __builtin_inject(tok);
    }
    static_assert(y == 5);

    struct Point { int x, y; };
    constexpr Point p = {.x=1, .y=2};
    consteval {
        __builtin_inject(^^{
            constexpr int px = \(p).x;
        });
    }
    static_assert(px == 1);

    consteval auto make_seq(int i) -> info {
        return ^^{
            constexpr int z = \(i);
        };
    }
    consteval {
        __builtin_inject(make_seq(10));
    }
    static_assert(z == 10);
}

namespace N3 {
    constexpr info r = ^^int;
    consteval {
        __builtin_inject(^^{
            constexpr \(r) v = 12;
        });
    }
    static_assert(v == 12);
    static_assert(^^decltype(v) == ^^int const);

    consteval auto make_variable(info ty) -> info {
        return ^^{
            \(ty) var = {};
        };
    }
    consteval {
        __builtin_inject(make_variable(^^char));
    }
    static_assert(^^decltype(var) == ^^char);
}

namespace N4 {
    template <bool B, typename T>
    struct enable_if {
        consteval {
            if (B) {
                __builtin_inject(^^{ using type1 = T; });
                __builtin_inject(^^{ using type2 = \(^^T); });
            }
        }
    };

    using A1 = enable_if<true, int>::type1; // ok
    using B1 = enable_if<false, int>::type1; // expected-error {{no type named}}
    using A2 = enable_if<true, int>::type2; // ok
    using B2 = enable_if<false, int>::type2; // expected-error {{no type named}}

    template <auto V>
    struct constant {
        consteval {
            __builtin_inject(^^{
                static constexpr \(^^decltype(V)) value = \(V);
            });
        }
    };

    static_assert(constant<5>::value == 5);
}

namespace N5 {
    template <typename... Ts>
    struct tuple {
        consteval {
            info types[] = {^^Ts...};
            for (size_t k = 0; k != sizeof...(Ts); ++k) {
                __builtin_inject(^^{
                    \(types[k]) \(__builtin_id("_", k)) = \(types[k])();
                });
            }
        }

        template <size_t I>
        constexpr auto get() const -> auto const& {
            consteval {
                __builtin_inject(^^{
                    return \(__builtin_id("_", I));
                });
            }
        }
    };

    constexpr int i = 1;
    constexpr auto xs = tuple<int, int const*>{._0 = 2, ._1 = &i};
    static_assert(xs._0 == 2);
    static_assert(xs._1 == &i);
    static_assert(&xs.get<0>() == &xs._0);
    static_assert(&xs.get<1>() == &xs._1);
}

namespace N6 {
    consteval auto make_var(int X) -> info {
        return ^^{
            constexpr int injected_value = \(X);
        };
    }

    namespace inner { }

    consteval {
        __builtin_inject(^^inner, make_var(42));
    }
    static_assert(inner::injected_value == 42);

    consteval auto make_type(int X) -> info {
        auto L = [=] { return X;};
        return ^^{
            struct A {
                \(^^decltype(L)) l = \(L);
            };
        };
    }

    auto check() -> void {
        consteval {
            __builtin_inject(^^inner, make_type(10));
        }
        inner::A a;
        a.l();
    }
}

namespace N7 {
    template <class S>
    consteval auto make_field(info type, S name, int val) -> info {
        return ^^{ \(type) \(__builtin_id(name)) = \(val); };
    }

    consteval auto make_field2(info type, string_view name, info init) {
        return ^^{ \(type) \(__builtin_id(name)) \(init); };
    }

    struct S {
        consteval {
            __builtin_inject(make_field(^^int, "x", 1));
            __builtin_inject(make_field(^^int, string_view("y"), 2));
            __builtin_inject(make_field2(^^int, "z", ^^{ = 3 }));
        }
    };

    static_assert(sizeof(S) == 3 * sizeof(int));
    constexpr auto check() -> int {
        S s = {};
        return s.x + s.y + s.z;
    }
    static_assert(check() == 6);
}

namespace N8 {
    template <int I, class... Ts>
    constexpr auto nth(Ts... ts) {
        consteval {
            __builtin_inject(^^{
                return ts...[\(I)];
            });
        }
    }

    template <int I, class... Ts>
    constexpr auto nth_local(Ts... ts) {
        consteval {
            info vars[] = {^^ts...};
            __builtin_inject(^^{
                return \(vars[I]);
            });
        }
    }

    constexpr int v = 1;
    static_assert(nth<0>(1) == 1);
    static_assert(nth<0>(&v) == &v);
    static_assert(nth<0>(1, &v) == 1);
    static_assert(nth<1>(1, &v) == &v);

    static_assert(nth_local<0>(1) == 1);
    static_assert(nth_local<0>(&v) == &v);
    static_assert(nth_local<0>(1, &v) == 1);
    static_assert(nth_local<1>(1, &v) == &v);
}

namespace N9 {
    struct A { int m; };
    struct B { int m; };

    template <class T>
    consteval auto get_m(T x) -> int {
        consteval {
            auto r = ^^A::m;
            __builtin_inject(^^{
                return x.\(r); // expected-error {{class 'B' not derived from}}
            });
        }
    }

    static_assert(get_m(A{.m=1}) == 1);
    static_assert(get_m(B{.m=1}) == 1); // expected-error {{static assert}}
}

namespace N10 {
    consteval void bad_report() {
        string_view sv("hello");
        __builtin_report_tokens(sv, ^^{ int x; }); // expected-error {{expected string literal in '__builtin_report_tokens'}}
    }
}

namespace N11 {
    static_assert(^^{ } == ^^{ });
    static_assert(^^{ , } == ^^{ , });
    static_assert(^^{ \(__builtin_id("x", 1)) } == ^^{ x1 });
}

namespace N12 {
    // Test that __builtin_inject handles operator info() conversion.
    struct Builder {
        info body = ^^{};
        consteval auto operator+=(info tok) -> void {
            body = ^^{ \(body) \(tok) };
        }
        consteval operator info() const { return body; }
    };

    template <class T>
    struct S {
        consteval {
            Builder b;
            b += ^^{ static constexpr int x = 1; };
            __builtin_inject(b);
        }
    };

    static_assert(S<int>::x == 1);
}

namespace N13 {
    // Injected member function templates should correctly substitute their
    // own template arguments, not the enclosing class template's arguments.
    template <class Outer> struct S {
        template <class T> static constexpr int val = sizeof(T);

        consteval {
            __builtin_inject(^^{
                template <class T> constexpr auto get(T ) -> int {
                    return val<T>;
                }
            });
        }
    };

    static_assert(S<char>::val<int> == sizeof(int));
    static_assert(S<char>{}.get(42) == sizeof(int));
    static_assert(S<char>{}.get('x') == sizeof(char));
}

namespace N14 {
    struct Base {
        consteval operator info() const {
            return ^^{ static constexpr int inherited_conv_ok = 1; };
        }
    };
    struct Derived : Base { };

    consteval {
        Derived d;
        __builtin_inject(d);
    }
    static_assert(inherited_conv_ok == 1);

    struct OnlyRvalue {
        consteval operator info() && {
            return ^^{ static constexpr int refqual_conv_bug = 1; };
        }
    };

    consteval { // expected-error {{evaluating expression of a consteval block must be a constant expression}}
        OnlyRvalue x;
        __builtin_inject(x);
    }
    static_assert(refqual_conv_bug == 1); // expected-error {{use of undeclared identifier 'refqual_conv_bug'}}
}

namespace N15 {
    // Conversion overload ranking should pick the non-const overload for a
    // non-const lvalue object.
    struct X {
        consteval operator info() const {
            return ^^{ static constexpr int choose = 1; };
        }
        consteval operator info() {
            return ^^{ static constexpr int choose = 2; };
        }
    };

    consteval {
        X x;
        __builtin_inject(x);
    }
    static_assert(choose == 2);
}

namespace N16 {
    // Conversion function templates to info should participate.
    struct X {
        template <class T>
        requires __is_same(T, info)
        consteval operator T() const {
            return ^^{ static constexpr int conv_template_ok = 1; };
        }
    };

    consteval {
        X x;
        __builtin_inject(x);
    }
    static_assert(conv_template_ok == 1);
}

namespace N17 {
    // Using-declarations that introduce operator info() should be considered.
    struct B {
        consteval operator info() const {
            return ^^{ static constexpr int using_conv_ok = 1; };
        }
    };

    struct D : B {
        using B::operator info;
    };

    consteval {
        D d;
        __builtin_inject(d);
    }
    static_assert(using_conv_ok == 1);
}

namespace N18 {
    // Empty token sequences are a no-op when injected.
    consteval { __builtin_inject(^^{}); }
    consteval { __builtin_inject(^^{ }); }
    static_assert(true);
}

namespace N19 {
    // Nested braces inside ^^{ ... } are captured as part of the token stream.
    consteval int n_made = 0;
    consteval {
        __builtin_inject(^^{
            constexpr int nested_block_value = []{
                int x = 1;
                { int y = 2; x += y; { x += 7; } }
                return x;
            }();
        });
    }
    static_assert(nested_block_value == 10);
}

namespace N20 {
    // Negative: injecting a non-token-sequence reflection.
    constexpr info type_refl = ^^int;
    consteval { // expected-error {{evaluating expression of a consteval block must be a constant expression}}
        __builtin_inject(type_refl);
    }
}

namespace N21 {
    // __builtin_inject inside a non-consteval, non-constexpr context is
    // accepted at parse time but never evaluated, so it injects nothing.
    info make_seq() {
        return ^^{ int q = 1; };
    }
    void caller() {
        __builtin_inject(make_seq());
    }
}

namespace N22 {
    // Interpolating various reflection kinds.
    constexpr int x = 7;
    constexpr float f = 1.5f;
    consteval auto cstr() -> info { return ^^{ "hello" }; }
    consteval {
        // int, float, type, identifier, function-name
        __builtin_inject(^^{ constexpr int kInt = \(x); });
        __builtin_inject(^^{ constexpr float kFloat = \(f); });
        __builtin_inject(^^{ using KType = \(^^int); });
        __builtin_inject(^^{ constexpr auto kStr = \(cstr()); });
    }
    static_assert(kInt == 7);
    static_assert(kFloat == 1.5f);
    static_assert(sizeof(KType) == sizeof(int));
}

namespace N23 {
    // Block-scope injection runs into the consteval lambda body, not the
    // enclosing function. Document that the injected name is NOT visible
    // in the outer scope. (When this changes, the FIXME below fires.)
    consteval auto add_x() -> info { return ^^{ int x = 5; (void)x; }; }
    constexpr int caller() {
        consteval { __builtin_inject(add_x()); }
        // FIXME: x = 5 was injected into the consteval lambda's compound
        // and isn't visible here. Update this test if/when block-scope
        // injection targets the enclosing function body.
        return 0;
    }
    static_assert(caller() == 0);
}

namespace N24 {
    // Dependent token sequences capture template parameters and resolve
    // at instantiation.
    template <typename T, T V>
    struct holder {
        consteval {
            __builtin_inject(^^{ using element = T; });
            __builtin_inject(^^{ static constexpr T value = V; });
        }
    };
    using H = holder<int, 42>;
    static_assert(H::value == 42);
    static_assert(sizeof(H::element) == sizeof(int));
}

namespace N25 {
    // Negative: malformed injected tokens diagnose at re-parse time.
    consteval {
        __builtin_inject(^^{ int int q; }); // expected-error {{cannot combine with previous 'int' declaration specifier}}
    }
}

namespace N26 {
    // Two-arg __builtin_inject with a non-namespace target diagnoses.
    struct S {};
    consteval { // expected-error {{evaluating expression of a consteval block must be a constant expression}}
        __builtin_inject(^^S, ^^{ int z = 0; });
    }
}
