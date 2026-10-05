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

// An annotation's on_complete callback runs for an enumeration once it is
// complete: after the last enumerator of its definition, or at an
// opaque-enum-declaration. A unary queue_injection in the callback -- of a
// class's annotation as well as an enumeration's -- injects into the nearest
// namespace enclosing the type, where argument-dependent lookup finds what it
// declares.

#include <meta>
#include <utility>

// ------------------------------------------------------- bitmask types ----

namespace lib {
struct BitmaskType {
  static consteval auto on_complete(std::meta::info ty) -> void {
    queue_injection(^^{
      constexpr auto operator+(\(ty) e) { return std::to_underlying(e); }
    });

    using enum std::meta::operators;
    for (auto op : {op_pipe, op_ampersand, op_caret}) {
      queue_injection(^^{
        constexpr auto operator \(op)(\(ty) lhs, \(ty) rhs) -> \(ty) {
          return \(ty)(+lhs \(op) +rhs);
        }
      });
    }
  }
};
inline constexpr auto bitmask_type = BitmaskType();
} // namespace lib

namespace N {
enum class [[=lib::bitmask_type]] Permission : int {
  None    = 0,
  Read    = 1 << 0,
  Write   = 1 << 1,
  Execute = 1 << 2,
};
} // namespace N

static_assert((N::Permission::Read | N::Permission::Write) == N::Permission(3));
static_assert((N::Permission(7) & N::Permission::Write) == N::Permission::Write);
static_assert((N::Permission::Read ^ N::Permission(3)) == N::Permission::Write);
static_assert(+N::Permission::Execute == 4);

// A member enumeration: the operators go to the enclosing namespace, not into
// the class.
namespace N {
struct File {
  enum class [[=lib::bitmask_type]] Mode : unsigned { In = 1, Out = 2 };
};
} // namespace N
static_assert((N::File::Mode::In | N::File::Mode::Out) == N::File::Mode(3));

// A member of a class template: per specialization (here when the scoped
// enumeration's definition is instantiated, on use).
namespace N {
template <class T>
struct Holder {
  enum class [[=lib::bitmask_type]] Flags : int { A = 1, B = 2 };
};
} // namespace N
static_assert((N::Holder<int>::Flags::A | N::Holder<int>::Flags::B) ==
              N::Holder<int>::Flags(3));

// Local to a function template: per instantiation, at namespace scope.
template <class T>
constexpr int local() {
  enum class [[=lib::bitmask_type]] Bits : int { A = 1, B = 2 };
  return +(Bits::A | Bits::B);
}
static_assert(local<int>() == 3);
static_assert(local<long>() == 3);

// --------------------------------------------- when the callback runs -----

// It observes every enumerator. (At an opaque-enum-declaration there is no
// enumerator-list yet, so the type is not enumerable: enumerators_of would not
// be a constant expression.)
struct count_enumerators {
  consteval void on_complete(std::meta::info e) const {
    int n = is_enumerable_type(e) ? int(enumerators_of(e).size()) : -1;
    queue_injection(^^{
      constexpr int \(std::meta::id(identifier_of(e), "_count")) =
          \(std::meta::reflect_constant(n));
    });
  }
};

enum class [[=count_enumerators{}]] Color { red, green, blue };
static_assert(Color_count == 3);

// An opaque-enum-declaration is complete: the callback runs there...
enum class [[=count_enumerators{}]] Opaque : int;
static_assert(Opaque_count == -1);

// ...and a later definition does not run it again (it would redeclare
// Opaque_count); only the annotations written on the definition run there.
enum class Opaque : int { a, b };

struct mark_defined {
  consteval void on_complete(std::meta::info e) const {
    queue_injection(^^{
      constexpr int \(std::meta::id(identifier_of(e), "_defined")) =
          \(std::meta::reflect_constant(int(enumerators_of(e).size())));
    });
  }
};
enum class [[=count_enumerators{}]] Later : int;
enum class [[=mark_defined{}]] Later : int { x, y, z };
static_assert(Later_count == -1 && Later_defined == 3);

// ------------------------------------------------------ nested classes ----

// A nested class's callback also injects into the namespace, not into the
// enclosing class still being defined.
struct name_tag {
  consteval void on_complete(std::meta::info c) const {
    queue_injection(^^{
      constexpr const char* \(std::meta::id(identifier_of(c), "_name")) =
          \(std::meta::str_lit(identifier_of(c)));
    });
  }
};

namespace M {
struct Outer {
  struct [[=name_tag{}]] Inner {};
  int x;
};
} // namespace M
static_assert(M::Inner_name[0] == 'I');
static_assert(sizeof(M::Outer) == sizeof(int));

int main(int, char**) { return 0; }
