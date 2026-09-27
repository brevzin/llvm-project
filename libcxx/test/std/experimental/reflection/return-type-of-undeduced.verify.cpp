//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection -std=c++2d

// return_type_of() on a function whose placeholder return type has not been
// deduced yet is not a constant expression. (It used to hang the compiler.)

#include <meta>

auto later();

constexpr auto of_function = std::meta::return_type_of(^^later);
// expected-error@-1 {{constexpr variable 'of_function' must be initialized by a constant expression}}
// expected-note@*:* {{subexpression not valid in a constant expression}}
// expected-note@-3 {{in call to 'return_type_of(^^(declaration))'}}
// expected-note@-4 {{cannot query the return type of function 'later', which has not been deduced yet}}

constexpr auto of_function_type =
    std::meta::return_type_of(type_of(^^later));
// expected-error@-2 {{constexpr variable 'of_function_type' must be initialized by a constant expression}}
// expected-note@*:* {{subexpression not valid in a constant expression}}
// expected-note@-3 {{in call to 'return_type_of(^^(type))'}}
// expected-note@-4 {{cannot query the return type of function type 'auto ()', which has not been deduced yet}}

// The type itself can still be reflected.
static_assert(type_of(^^later) != ^^int());

// Once the definition has deduced it, the return type is available.
auto later() { return 1; }
static_assert(std::meta::return_type_of(^^later) == ^^int);

// The case that is easy to hit: a macro asking about the function it is
// expanding into, from inside the body of an 'auto' function -- whose return
// type is exactly what is being deduced.
__macro enclosing_return_type() {
  (void)std::meta::return_type_of(std::meta::macro_expansion_context());
  return ^^{ 0 };
}

int known() { return enclosing_return_type!(); } // OK

auto deduced() { return enclosing_return_type!(); }
// expected-error@-1 {{expression macro 'enclosing_return_type' did not produce a token sequence}}
// expected-note@*:* {{subexpression not valid in a constant expression}}
// expected-note@*:* {{in call to 'return_type_of(^^(declaration))'}}
// expected-note@*:* {{in call to 'enclosing_return_type()'}}
// expected-note@*:* {{cannot query the return type of function 'deduced', which has not been deduced yet}}

// The failed invocation does not go on to deduce 'deduced' as returning void.
int use_deduced() { return deduced(); }
