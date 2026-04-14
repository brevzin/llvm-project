// RUN: %clang_cc1 -std=c++26 -freflection -fexpansion-statements -verify -verify-ignore-unexpected=note %s

using info = decltype(^^::);
using size_t = decltype(sizeof(0));

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
