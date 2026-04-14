//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20
// ADDITIONAL_COMPILE_FLAGS: -freflection-latest

// RUN: %{build}
// RUN: %{exec} %t.exe

#include <experimental/meta>
#include <format>
#include <cassert>

#include "test_macros.h"
#include "assert_macros.h"
#include "concat_macros.h"

namespace N {
    struct DebugFormatter {
        constexpr auto parse(auto& ctx) { return ctx.begin(); }

        template <class T>
        auto format(T const& object, auto& ctx) const {
            auto out = ctx.out();
            out = std::format_to(out, "{}{{", display_string_of(^^T));

            auto delim = [first=true, &out]() mutable {
                if (not first) {
                    *out++ = ',';
                    *out++ = ' ';
                }
                first = false;
            };

            constexpr auto ac = std::meta::access_context::unchecked();

            template for (constexpr auto nsdm : define_static_array(nonstatic_data_members_of(^^T, ac))) {
                delim();
                out = std::format_to(out, ".{}={}", identifier_of(nsdm), object.[:nsdm:]);
            }

            *out++ = '}';
            return out;
        }
    };

    struct DeriveDebug {
        consteval auto on_complete(std::meta::info ty) const -> void {
            __builtin_inject(^^std, ^^{
                template <>
                struct formatter<\(ty)> : ::N::DebugFormatter { };
            });
        }
    };

    inline constexpr DeriveDebug derive_debug{};
}

struct [[=N::derive_debug]] Config {
    std::string name;
    int amount;
};

// template <> struct std::formatter<Config> : N::DebugFormatter { };

template <class... Args>
auto test(std::string_view expected, std::format_string<Args...> fmt, Args&&... args) {
    std::string out = std::format(fmt, std::forward<Args>(args)...);
    TEST_REQUIRE(
        out == expected,
        TEST_WRITE_CONCATENATED(
            "\nFormat string   ", fmt.get(), "\nExpected output ", expected, "\nActual output   ", out, '\n'));
}

int main() {
    test("Config{.name=abc, .amount=10}", "{}", Config{.name="abc", .amount=10});
}
