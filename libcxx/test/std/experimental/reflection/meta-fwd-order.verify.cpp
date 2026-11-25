//===----------------------------------------------------------------------===//
//
// Copyright 2026 Jump Trading LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03 || c++11 || c++14 || c++17 || c++20 || c++23
// ADDITIONAL_COMPILE_FLAGS: -freflection

// <meta_fwd> stands in for <meta> where only functions compiled elsewhere are
// called. The two can be included together in one order only, and the other
// says so (once).

#include <meta_fwd>

std::meta::token_sequence declared_by_meta_fwd;
constexpr std::meta::info also_declared = ^^int;

#include <meta>
// expected-error@*:* {{<meta_fwd> was included before <meta> in this translation unit; include <meta> first (after it, <meta_fwd> does nothing)}}
