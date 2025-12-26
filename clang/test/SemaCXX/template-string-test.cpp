// RUN: %clang_cc1 -std=c++26 -freflection -fsyntax-only -verify %s

// Test basic template string with local variables
void test_local_vars() {
  int x = 42;
  double y = 3.14;

  auto s1 = t"x={x}, y={y}";
  // Check that we get the format string
  static_assert(__builtin_strcmp(s1.fmt, "x={}, y={}") == 0);

  // Check that the fields have the right values
  static_assert((^^decltype(s1._0)) == (^^int&));
  static_assert((^^decltype(s1._1)) == (^^double&));
}

// Test with expressions
void test_expressions() {
  int a = 10;
  int b = 20;

  auto s2 = t"sum={a+b}, product={a*b}";
  static_assert(__builtin_strcmp(s2.fmt, "sum={}, product={}") == 0);
  static_assert((^^decltype(s2._0)) == (^^int));
  static_assert((^^decltype(s2._1)) == (^^int));
}

// Test with function calls
double compute() { return 3.14159; }

void test_function_calls() {
  auto s3 = t"pi={compute()}";
  static_assert(__builtin_strcmp(s3.fmt, "pi={}") == 0);
  static_assert((^^decltype(s3._0)) == (^^double));
}

// Test escaped braces
void test_escaped_braces() {
  int x = 5;
  auto s4 = t"{{x}}={x}";
  static_assert(__builtin_strcmp(s4.fmt, "{{x}}={}") == 0);
}

// Test empty template string
void test_empty() {
  auto s5 = t"";
  static_assert(__builtin_strcmp(s5.fmt, "") == 0);
}

// Test just text, no expressions
void test_no_expressions() {
  auto s6 = t"Hello\n\"World!\"";
  static_assert(__builtin_strcmp(s6.fmt, "Hello\n\"World!\"") == 0);
}

// Test specifiers
void test_fmt_specifiers() {
  int i = 42;
  auto s7 = t"{i:#x}";
  static_assert(__builtin_strcmp(s7.fmt, "{:#x}") == 0);
}

// Test parentheses
namespace N {
  int C = 42;
}
void test_balanced_tokens() {
  double N = 0.0;
  auto s8 = t"{N::C}";
  auto s9 = t"{(N::C)}";
  static_assert(__builtin_strcmp(s8.fmt, "{::C}") == 0);
  static_assert((^^decltype(s8._0)) == (^^double&));
  static_assert(__builtin_strcmp(s9.fmt, "{}") == 0);
  static_assert((^^decltype(s9._0)) == (^^int&));
}


// Test nested braces in expressions (e.g., initializer lists, lambdas)
void test_nested_braces() {
  struct Point { int x, y; };
  Point p{1, 2};

  // Nested braces in compound literal
  auto s10 = t"point={Point{3, 4}.x}";
  static_assert(__builtin_strcmp(s10.fmt, "point={}") == 0);
  static_assert((^^decltype(s10._0)) == (^^int&&));

  // Lambda with braces
  auto s11 = t"lambda={[](){ return 42; }()}";
  static_assert(__builtin_strcmp(s11.fmt, "lambda={}") == 0);
}

// Test string literals inside expressions
void test_string_in_expr() {
  const char* msg = "hello";
  auto s12 = t"msg={msg}, literal={"world"}";
  static_assert(__builtin_strcmp(s12.fmt, "msg={}, literal={}") == 0);
  static_assert((^^decltype(s12._0)) == (^^const char*&));
  static_assert((^^decltype(s12._1)) == (^^const char (&)[6]));
}

// Test complex operators
void test_complex_operators() {
  int a = 5, b = 3, c = 2;

  // Ternary operator
  auto s13 = t"max={(a > b ? a : b)}";
  static_assert(__builtin_strcmp(s13.fmt, "max={}") == 0);
  static_assert((^^decltype(s13._0)) == (^^int&));

  // Comma operator
  auto s14 = t"comma={(a++, b++, c)}";
  static_assert(__builtin_strcmp(s14.fmt, "comma={}") == 0);
  static_assert((^^decltype(s14._0)) == (^^int&));
}

// Test member access and array subscripts
void test_member_access() {
  struct S { int x; int arr[3]; };
  S s{42, {1, 2, 3}};
  S* ptr = &s;

  auto s15 = t"member={s.x}, array={s.arr[1]}, ptr={ptr->x}";
  static_assert(__builtin_strcmp(s15.fmt, "member={}, array={}, ptr={}") == 0);
  static_assert((^^decltype(s15._0)) == (^^int&));
  static_assert((^^decltype(s15._1)) == (^^int&));
  static_assert((^^decltype(s15._2)) == (^^int&));
}

// Test sizeof and cast expressions
void test_sizeof_and_casts() {
  double d = 3.14;

  auto s16 = t"size={sizeof(int)}, cast={static_cast<int>(d)}";
  static_assert(__builtin_strcmp(s16.fmt, "size={}, cast={}") == 0);
  static_assert((^^decltype(s16._0)) == (^^unsigned long));
  static_assert((^^decltype(s16._1)) == (^^int));
}

// Test multiple escaped braces in a row
void test_multiple_escaped_braces() {
  int x = 5;
  auto s17 = t"{{{{x}}}}={x}";  // Should produce "{{x}}={}"
  static_assert(__builtin_strcmp(s17.fmt, "{{{{x}}}}={}") == 0);

  auto s18 = t"{{{{{{}}}}}}";  // Should produce "{{{}}}"
  static_assert(__builtin_strcmp(s18.fmt, "{{{{{{}}}}}}") == 0);
}

// Test edge case: expression that looks like it contains braces
void test_confusing_expressions() {
  char arr[] = "a{b}c";
  auto s19 = t"str={arr[2]}";  // arr[2] is '}' character
  static_assert(__builtin_strcmp(s19.fmt, "str={}") == 0);
  static_assert((^^decltype(s19._0)) == (^^char&));
}

// Test very long expressions
void test_long_expressions() {
  int a = 1, b = 2, c = 3, d = 4, e = 5;
  auto s20 = t"long={a + b * c - d / e + (a << b) | (c & d) ^ e}";
  static_assert(__builtin_strcmp(s20.fmt, "long={}") == 0);
}

// Test template expressions (if in template context)
template<typename T>
void test_template_expr() {
  T val{};
  auto s21 = t"type_size={sizeof(T)}, val={val}";
  static_assert(__builtin_strcmp(s21.fmt, "type_size={}, val={}") == 0);
}

void test_template_expr_impl() {
  test_template_expr<int>();
  test_template_expr<char>();
}

// Test mixing escaped braces with expressions
void test_mixed_braces() {
  int x = 10;
  auto s22 = t"{{before{x}after}}";  // {{ then {x} then }}
  static_assert(__builtin_strcmp(s22.fmt, "{{before{}after}}") == 0);

  auto s23 = t"{x}{{middle}}{x}";  // {x} then {{ then }} then {x}
  static_assert(__builtin_strcmp(s23.fmt, "{}{{middle}}{}") == 0);
}

void test_trailing_whitespace() {
  int x = 5;
  auto s23 = t"x={x }, y={x}";
  static_assert(__builtin_strcmp(s23.fmt, "x={}, y={}") == 0);
}

void test_trailing_eq() {
  int x = 5;
  static_assert(__builtin_strcmp(decltype(t"{x=}")::fmt, "x={}") == 0);
  static_assert(__builtin_strcmp(decltype(t"{x = }")::fmt, "x = {}") == 0);
  static_assert(__builtin_strcmp(decltype(t"{x + 1 = :#x}")::fmt, "x + 1 = {:#x}") == 0);
}

// expected-no-diagnostics
