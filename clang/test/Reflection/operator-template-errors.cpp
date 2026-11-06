// RUN: %clang_cc1 -std=c++2d -freflection -verify %s
// RUN: %clang_cc1 -std=c++26 -freflection -verify=cxx26 %s

// P4340 ext: 'operator template' -- what can't be declared.

namespace std::meta {
  using info = decltype(^^::);
}

struct A {
  int x;
  std::meta::info operator template() const; // expected-error {{'operator template' cannot have a return type}}
                                             // cxx26-error@-1 {{'operator template' cannot have a return type}}
};
struct B {
  int x;
  consteval auto operator template() const -> std::meta::info; // expected-error {{'operator template' cannot have a return type}}
                                                               // cxx26-error@-1 {{'operator template' cannot have a return type}}
};
struct C {
  int x;
  static consteval operator template(); // expected-error {{overloaded 'operator template' cannot be a static member function}}
                                        // cxx26-error@-1 {{'operator template' requires C++29 and '-freflection'}}
};
struct D {
  int x;
  consteval operator template() const = default; // cxx26-error {{'operator template' requires C++29 and '-freflection'}}
};
consteval operator template(); // expected-error {{overloaded 'operator template' must be a non-static member function}}
                               // cxx26-error@-1 {{'operator template' requires C++29 and '-freflection'}}

