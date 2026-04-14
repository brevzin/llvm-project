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
