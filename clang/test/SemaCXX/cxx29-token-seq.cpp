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
                return x.\(r); // expected-error {{class 'N9::B' not derived from}}
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
