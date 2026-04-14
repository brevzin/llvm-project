// RUN: %clang_cc1 -std=c++26 -freflection -fexpansion-statements -verify -verify-ignore-unexpected=note %s

using info = decltype(^^::);

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
