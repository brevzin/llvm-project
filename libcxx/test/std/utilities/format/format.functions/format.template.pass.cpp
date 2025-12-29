#include <format>
#include <string_view>

#include "test_macros.h"
#include "assert_macros.h"
#include "concat_macros.h"

auto check_eq(std::string_view actual, std::string_view expected) -> void {
    TEST_REQUIRE(
        expected == actual,
        TEST_WRITE_CONCATENATED(
            "Exected output ", expected, "\nActual output  ", actual, '\n'));
}

int main(int, char**) {
    int x = 42;
    check_eq(std::format(t"Got {x}"), "Got 42");
    check_eq(std::format(t"Got {x:#x}"), "Got 0x2a");
    check_eq(std::format(t"{x=}"), "x=42");
    check_eq(std::format(t"{x = }"), "x = 42");
    check_eq(std::format(t"{x=:#x}"), "x=0x2a");
}
