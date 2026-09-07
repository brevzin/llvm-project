// RUN: %clang_cc1 -std=c++26 -freflection -dump-tokens %s 2>&1 | FileCheck %s --check-prefix=TS
// RUN: %clang_cc1 -std=c++26 -dump-tokens %s 2>&1 | FileCheck %s --check-prefix=PLAIN

// t"..." only lexes as a template string under -freflection; otherwise it is
// an identifier followed by a string literal.
auto s = t"{x}";
// TS: template_string_begin 't\"{'
// TS: identifier 'x'
// TS: template_string_end '}\"'
// PLAIN: identifier 't'
// PLAIN: string_literal '\"{x}\"'
