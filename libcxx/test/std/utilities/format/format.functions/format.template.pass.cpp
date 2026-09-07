//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection

// Template string literals (P3951) with std::format, std::format_to,
// std::print, std::println and operator""s.

#include <format>
#include <ostream>
#include <print>
#include <string_view>
#include <sstream>

#include "test_macros.h"
#include "assert_macros.h"
#include "concat_macros.h"

auto check_eq(std::string_view actual, std::string_view expected) -> void {
    TEST_REQUIRE(
        expected == actual,
        TEST_WRITE_CONCATENATED(
            "Exected output ", expected, "\nActual output  ", actual, '\n'));
}

template <class F>
auto test_function(F f) -> void {
    int x = 42;
    check_eq(f(t"Got {x}"), "Got 42");
    check_eq(f(t"Got {x:#x}"), "Got 0x2a");
    check_eq(f(t"{x=}"), "x=42");
    check_eq(f(t"{x = }"), "x = 42");
    check_eq(f(t"{x=:#x}"), "x=0x2a");

    int width = 10;
    check_eq(f(t"{x:*^{width}}"), "****42****");
}

int main(int, char**) {
    // std::format
    test_function([](auto&& s){ return std::format(s); });

    // std::format_to
    char buffer[1000];
    test_function([&](auto&& s){
        auto out = std::format_to(buffer, s);
        return std::string_view(buffer, out - buffer);
    });

    // std::print
    test_function([&](auto&& s){
        std::stringstream sstr;
        std::print(sstr, s);
        return sstr.str();
    });

    // operator""
    using namespace std::literals;
    int x = 42;
    check_eq(t"Got {x}"s, "Got 42");

    // quick test for std::println
    std::stringstream sstr;
    std::println(sstr, t"Got {x}");
    check_eq(sstr.str(), "Got 42\n");

    // weak template
    struct W {
        auto fmt() const { return std::dynamic_format("Got {}"); }
        auto exprs() const -> W const& { return *this; }
        int x;
    };
    check_eq(std::format(W{.x=42}), "Got 42");
}
