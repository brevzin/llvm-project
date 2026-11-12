// [[=cached]]: memoize a function by rewriting its body, in the spirit of
// Rust's #[cached] and Swift's body macros.
//
//     [[=cached]] auto fib(u64 n) -> u64 {
//         return n < 2 ? n : fib(n - 1) + fib(n - 2);
//     }
//
// becomes, in effect,
//
//     auto fib(u64 n) -> u64 {
//         using R = <fib's declared return type>;
//         auto body = [&]() -> R { <the original body> };
//         if consteval { return body(); }            // no cache in constant evaluation
//         else {
//             static cache<std::tuple<u64>, R> c;     // one per function (per specialization)
//             ...look up, else run body() and insert...
//         }
//     }
//
// Everything marked NEEDS is missing from this fork today:
//
//   NEEDS (1) on_definition: an annotation callback that runs when the parser
//             reaches the definition of a function carrying the annotation,
//             before its body is parsed. It receives a reflection of the
//             (declared, not yet defined) function and the tokens of its
//             function-body, and returns the tokens of the replacement
//             function-body. For a function template it runs once, on the
//             pattern; the replacement is ordinary template code.
//             (It could later offer expand_at_instantiation(), as expression
//             macros do, for callbacks that need instantiated types; cached
//             does not.) A callback that reports consteval_error_str rejects
//             the definition.
//   NEEDS (2) parameters_of(fn) for a function template's pattern: the
//             parameters' reflections, on which identifier_of and
//             has_identifier work. (type_of may stay an error: cached never
//             asks for a parameter's type -- the generated code uses
//             decltype, resolved at instantiation.)
//   NEEDS (3) is_function_parameter_pack(p).
//   NEEDS (4) has_deduced_return_type(fn): the return type is a placeholder
//             (auto, decltype(auto)) not yet deduced -- as it always is when
//             on_definition runs, before the body is parsed.
//   NEEDS (5) Inside on_definition, identifier_of(p) answers with the
//             *definition's* parameter names (the ones the body uses), even
//             when an earlier declaration named them differently -- where
//             P2996 makes identifier_of ill-formed.
//
// Already available, and used below: access_context::current().scope() in a
// function body, with return_type_of on it, gives the declared return type
// -- concrete, or dependent in a template (and, usefully, refuses a deduced
// one that has not been deduced yet).
//
// The generated names (cached_R, cached_body, ...) are visible to the
// original body, which is parsed inside them: a body using one of those
// names would see the wrong entity. That is the (parked) hygiene problem.
//
// Rejected, with a message: constructors, destructors, conversion functions,
// non-static member functions (one static cache would be shared by every
// object), C varargs, unnamed parameters, deduced return types (recursion
// through the cache would use fib before its type is deduced), coroutines (a
// co_return inside the wrapping lambda would mean something else), and -- in
// the generated code, since a template's return type is only known per
// specialization -- void and reference return types.
//
// Build today (simulation mode, see the end of the file):
//   clang++ $(cat ~/tmp/flags2d) -stdlib=libc++ ... examples/cached.cxx

#include <debugging>
#include <functional>
#include <meta>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

// ----------------------------------------------------------- the cache -----

namespace cached_detail {

template <class Key>
struct tuple_hash;

template <class... Ts>
struct tuple_hash<std::tuple<Ts...>> {
  auto operator()(std::tuple<Ts...> const& t) const -> std::size_t {
    return std::apply(
        [](auto const&... e) {
          std::size_t h = 0;
          ((h ^= std::hash<std::remove_cvref_t<decltype(e)>>{}(e) +
                 0x9e3779b97f4a7c15 + (h << 6) + (h >> 2)),
           ...);
          return h;
        },
        t);
  }
};

// The lock is held only to look up and to insert, never while the function
// body runs: the body recurses through the cached function, and holding the
// lock there would deadlock. Two threads may occasionally compute the same
// entry; the first insert wins.
template <class Key, class R>
class cache {
  std::mutex mutex;
  std::unordered_map<Key, R, tuple_hash<Key>> map;

public:
  auto find(Key const& key) -> std::optional<R> {
    std::lock_guard lock(mutex);
    if (auto it = map.find(key); it != map.end())
      return it->second;
    return std::nullopt;
  }

  auto insert(Key key, R const& value) -> void {
    std::lock_guard lock(mutex);
    map.try_emplace(std::move(key), value);
  }
};

} // namespace cached_detail

// ----------------------------------------------- stand-ins (simulation) ----

// Stand-ins for NEEDS (3) and (4), good enough for the simulation below,
// which only uses non-template functions with written return types. Delete
// once the real metafunctions exist.
#ifndef CACHED_HAVE_PACK_AND_DEDUCED_QUERIES
namespace cached_detail {
consteval auto is_function_parameter_pack(std::meta::info) -> bool {
  return false;
}
consteval auto has_deduced_return_type(std::meta::info) -> bool {
  return false;
}
} // namespace cached_detail
#endif

// ------------------------------------------------------- the annotation ----

struct cached_t {
  // NEEDS (1): the callback itself.
  consteval auto on_definition(std::meta::info fn,
                               std::meta::token_sequence body) const
      -> std::meta::token_sequence {
    using namespace std::meta;
#ifndef CACHED_HAVE_PACK_AND_DEDUCED_QUERIES
    using cached_detail::has_deduced_return_type;
    using cached_detail::is_function_parameter_pack;
#endif

    auto fail = [&] [[noreturn]] (char const* why) {
      std::consteval_error_str("cached", std::string("[[=cached]] on '") +
                                             std::string(display_string_of(fn)) +
                                             "': " + why);
    };

    if (is_constructor(fn) || is_destructor(fn) || is_conversion_function(fn))
      fail("only functions with a return type can be cached");
    if (is_class_member(fn) && !is_static_member(fn))
      fail("a non-static member function's cache would be shared by "
           "every object; cache a static member or a free function");
    if (has_ellipsis_parameter(fn))
      fail("C varargs cannot be part of the cache key");
    if (has_deduced_return_type(fn)) // NEEDS (4)
      fail("the return type must be written: recursion through the "
           "cache would use the function before its return type is "
           "deduced");
    for (token_sequence tok : tokens_of(body))
      if (tok == ^^{ co_return } || tok == ^^{ co_await } ||
          tok == ^^{ co_yield })
        fail("a coroutine cannot be cached (its body cannot be "
             "wrapped in a lambda)");

    // The cache key: a tuple of the parameters, by value. A pack contributes
    // its elements -- 'xs...' -- which is all a template needs: the types are
    // written as decltype, so they resolve at instantiation.
    list_builder key_types(^^{, });
    list_builder key_args(^^{, });
    for (info p : parameters_of(fn)) { // NEEDS (2) for templates
      if (!has_identifier(p))
        fail("every parameter needs a name, to be part of the key");
      token_sequence name = id(identifier_of(p)); // NEEDS (5)
      if (is_function_parameter_pack(p)) {        // NEEDS (3)
        key_types += ^^{ std::remove_cvref_t<decltype(\(name))>... };
        key_args += ^^{ \(name)... };
      } else {
        key_types += ^^{ std::remove_cvref_t<decltype(\(name))> };
        key_args += name;
      }
    }

    // The original function-body (a compound statement, or a function-try-
    // block) becomes the body of a lambda whose return type is the declared
    // one: its returns -- 'return {1, 2};' included -- mean what they meant.
    // The declared return type comes from the function itself, concrete or
    // dependent alike.
    return ^^{ {
      using cached_R = [:std::meta::return_type_of(
          std::meta::access_context::current().scope()):];
      static_assert(!std::is_void_v<cached_R>,
                    "[[=cached]]: a function returning void has nothing to cache");
      static_assert(!std::is_reference_v<cached_R>,
                    "[[=cached]]: a reference cannot be cached");
      auto cached_body = [&]() -> cached_R { \(body) };
      if consteval {
        return cached_body();
      } else {
        using cached_key = std::tuple<\(key_types)>;
        static ::cached_detail::cache<cached_key, cached_R> cached_cache;
        cached_key cached_k{\(key_args)};
        if (auto cached_hit = cached_cache.find(cached_k))
          return *std::move(cached_hit);
        cached_R cached_result = cached_body();
        cached_cache.insert(std::move(cached_k), cached_result);
        return cached_result;
      }
    } };
  }
};

inline constexpr cached_t cached;

// ------------------------------------------------------------- examples ----

#include <string>
#include <vector>

using u64 = unsigned long long;
inline int calls = 0; // how often an original body ran

#ifdef CACHED_HAVE_ON_DEFINITION

// The intended use. NEEDS (1)-(5).

[[=cached]] auto fib(u64 n) -> u64 {
  ++calls;
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

// A template with a pack: the key is std::tuple<T, Ts...> per specialization.
template <class T, class... Ts>
[[=cached]] auto sum(T x, Ts... xs) -> T {
  ++calls;
  return (x + ... + xs);
}

// A dependent return type and a braced return.
template <class T>
[[=cached]] auto twice(T x) -> std::vector<T> {
  ++calls;
  return {x, x};
}

// Parameters by reference are copied into the key.
[[=cached]] auto length(std::string const& s) -> std::size_t {
  ++calls;
  return s.size();
}

// A function-try-block.
[[=cached]] auto checked(int n) -> int try {
  ++calls;
  return n * 2;
} catch (...) {
  return -1;
}

// constexpr: constant evaluation runs the body directly, uncached.
[[=cached]] constexpr auto square(int n) -> int { return n * n; }
static_assert(square(7) == 49);

struct S {
  [[=cached]] static auto triple(int n) -> int {
    ++calls;
    return 3 * n;
  }
};

// Rejected:
//   [[=cached]] auto f(int n) { return n; }    // deduced return type
//   [[=cached]] void g(int n) {}               // void (static_assert)
//   [[=cached]] auto h(int) -> int { ... }     // unnamed parameter
//   struct T { [[=cached]] auto m(int n) -> int; };  // non-static member

#else

// Simulation, compilable today: declare the function, then do what
// on_definition would -- call the callback with the declaration and the body
// tokens, and define the function with the result. (Only non-template
// functions: NEEDS (2) for templates.)

auto fib(u64 n) -> u64;
auto length(std::string const& s) -> std::size_t;
auto checked(int n) -> int;
constexpr auto square(int n) -> int;
struct S {
  static auto triple(int n) -> int;
};

consteval {
  using std::meta::token_sequence;
  token_sequence fib_body = cached.on_definition(^^fib, ^^{ {
    ++calls;
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
  } });
  queue_injection(^^{ auto fib(u64 n) -> u64 \(fib_body) });

  token_sequence length_body = cached.on_definition(^^length, ^^{ {
    ++calls;
    return s.size();
  } });
  queue_injection(^^{ auto length(std::string const& s) -> std::size_t
                          \(length_body) });

  token_sequence checked_body = cached.on_definition(^^checked, ^^{
    try {
      ++calls;
      return n * 2;
    } catch (...) {
      return -1;
    }
  });
  queue_injection(^^{ auto checked(int n) -> int \(checked_body) });

  token_sequence square_body =
      cached.on_definition(^^square, ^^{ { return n * n; } });
  queue_injection(^^{ constexpr auto square(int n) -> int \(square_body) });

  token_sequence triple_body = cached.on_definition(^^S::triple, ^^{ {
    ++calls;
    return 3 * n;
  } });
  queue_injection(^^{ auto S::triple(int n) -> int \(triple_body) });
}

static_assert(square(7) == 49);

#endif

#include <cstdio>

int main() {
  int failures = 0;
  auto check = [&](bool ok, char const* what) {
    if (!ok) {
      std::printf("FAIL: %s\n", what);
      ++failures;
    }
  };

  calls = 0;
  check(fib(90) == 2880067194370816120ull, "fib(90)");
  check(calls == 91, "fib: each n computed once");
  calls = 0;
  check(fib(90) == 2880067194370816120ull && calls == 0, "fib: cached");

  calls = 0;
  std::string hello = "hello";
  check(length(hello) == 5 && length("hello") == 5 && calls == 1, "length");

  calls = 0;
  check(checked(21) == 42 && checked(21) == 42 && calls == 1, "checked");

  calls = 0;
  check(S::triple(5) == 15 && S::triple(5) == 15 && calls == 1, "triple");

  check(square(9) == 81, "square at run time");

#ifdef CACHED_HAVE_ON_DEFINITION
  calls = 0;
  check(sum(1, 2, 3) == 6 && sum(1, 2, 3) == 6 && calls == 1, "sum");
  check(sum(1.5) == 1.5 && calls == 2, "sum: another specialization");
  calls = 0;
  check(twice(4) == std::vector{4, 4} && twice(4).size() == 2 && calls == 1,
        "twice");
#endif

  if (failures == 0)
    std::printf("all passed\n");
  return failures;
}
