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

// each!(int i, range, body;): a range loop, expanded as the loop that suits
// the range, with the body pasted into it (so break, continue, and return mean
// what they would in a for loop). An example, and a test of the pieces it
// uses: statement macros with raw and expression parameters, lookup,
// substitute, extract of a static member function, token rewriting, and
// unique_id.
//
// - The customization point is iter::loop_traits<R> (or, for a type you own,
//   a static member of R): loop(self, dir) returns the statements iterating
//   the range in direction dir, written like a generator -- 'co_yield E;'
//   produces an element -- and iterating other ranges with iter::each_dir!
//   (which is how views compose). The consumer's body is pasted at each
//   co_yield, so no coroutine is involved, and a co_yield in the consumer's
//   body is the consumer's own. The body may sit inside several of the view's
//   loops (join) or at several co_yields (concat), so its top-level break
//   becomes a jump past the whole loop, and it is wrapped in
//   'do { ... } while (false)', which a top-level continue leaves. A range
//   without a loop is iterated with its iterators.
// - The standard views' loops are specializations like any other: filter,
//   transform, take_while, reverse, join, and concat become loops over the
//   underlying ranges, calling each function only as often as needed, as in "Using Token
//   Sequences to Iterate Ranges" (brevzin.github.io, 2025). (Iterating
//   take_while | filter | reverse with iterators calls the predicates 8 and
//   10 times; this loop, 5 and 4.)
// - Every name the expansion declares is a unique_id: nested expansions (a
//   view iterating a view) cannot capture each other's names, nor the
//   consumer's. A view's own locals must be unique_ids for the same reason.

#include <meta>
#include <algorithm>
#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <iterator>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace iter {
using namespace std::meta;

enum class direction { forward, reverse };
consteval auto operator!(direction d) -> direction {
  return d == direction::forward ? direction::reverse : direction::forward;
}

// The customization point: how each! iterates a range of type R. A
// specialization (or, for a type you own, R itself) provides
//
//   static consteval auto loop(token_sequence self, direction dir)
//       -> token_sequence;
//
// returning the statements that iterate 'self' in direction 'dir', written
// like a generator: 'co_yield E;' produces the element E. 'self' is an
// expression naming the range, forwarded (an rvalue when the loop owns the
// range, so that '$self.base()' moves the base out). Other ranges -- its
// base, say -- are iterated with iter::each_dir!(dir, elem, range, body),
// which customizes them in turn. A range with neither is iterated with its
// iterators.
template <class R>
struct loop_traits {};

consteval auto paste_yields(token_sequence loop, token_sequence elem,
                            token_sequence body) -> token_sequence;

// R's loop function, if it has one.
consteval auto loop_function(info R) -> std::optional<info> {
  if (auto fn = lookup(substitute(^^loop_traits, {R}), "loop",
                       access_context::unchecked()))
    return fn;
  if (is_class_type(R))
    return lookup(R, "loop", access_context::unchecked());
  return std::nullopt;
}

// The statements iterating the range 'src' -- a forwarded expression, of type
// R -- in direction 'dir': each element bound by 'elem' (a declaration without
// its initializer), then 'body'.
consteval auto loop_for(info R, token_sequence src, direction dir,
                        token_sequence elem, token_sequence body)
    -> token_sequence {
  if (auto fn = loop_function(R)) {
    auto loop = extract<token_sequence (*)(token_sequence, direction)>(*fn);
    return paste_yields(loop(src, dir), elem, body);
  }

  // Any other range: its iterators (on an lvalue: ranges::begin rejects an
  // rvalue of a range that isn't borrowed).
  auto s = unique_id("s"), b = unique_id("b"), it = unique_id("it");
  if (dir == direction::forward)
    return ^^{
      auto&& $s = $src;
      for (auto $it = std::ranges::begin($s), $b = std::ranges::end($s);
           $it != $b; ++$it) {
        $elem = *$it;
        $body
      }
    };
  return ^^{
    auto&& $s = $src;
    for (auto $b = std::ranges::begin($s),
              $it = std::ranges::next($b, std::ranges::end($s));
         $it != $b;) {
      --$it;
      $elem = *$it;
      $body
    }
  };
}

// The consumer's body, with each top-level break -- not one inside the body's
// own loop or switch, or inside a macro invocation's arguments (an inner
// each!) -- replaced by 'on_break': a pasted body may sit inside several of
// the view's loops, all of which a break must leave. (A continue needs no
// rewriting: see paste_yields.)
consteval auto rewrite_breaks(token_sequence body, token_sequence on_break,
                              bool& used_break) -> token_sequence {
  struct frame {
    int brace_depth = -1;      // ends at this '}' (a braced body)...
    int paren_depth = -1;      // ...or at this ')' (macro arguments)...
    bool until_semi = false;   // ...or at the next ';' at its depth
    int braces = 0, parens = 0;
    bool is_do = false;
  };
  std::vector<frame> stack;
  token_sequence out;
  int braces = 0, parens = 0;
  bool after_do_body = false;
  auto end_frames = [&](auto pred) {
    while (!stack.empty() && pred(stack.back())) {
      after_do_body = stack.back().is_do;
      stack.pop_back();
    }
  };
  std::size_t n = body.size();
  for (std::size_t i = 0; i != n; ++i) {
    auto s = stringize(body[i]);
    if (s == "while" && after_do_body) {        // a do-while's condition
      after_do_body = false;
      out += body[i];
      continue;
    }
    after_do_body = false;
    if (s == "for" || s == "while" || s == "switch" || s == "do") {
      out += body[i];
      std::size_t j = i + 1;
      if (s != "do") {                           // the parenthesized header
        for (int d = 0; j != n; ++j) {
          auto h = stringize(body[j]);
          out += body[j];
          if (h == "(") ++d;
          else if (h == ")" && --d == 0) { ++j; break; }
        }
      }
      if (j != n && stringize(body[j]) == "{")
        stack.push_back({.brace_depth = braces + 1, .is_do = s == "do"});
      else
        stack.push_back({.until_semi = true, .braces = braces,
                         .parens = parens, .is_do = s == "do"});
      i = j - 1;
      continue;
    }
    if (token_kind_of(body[i]) == token_kind::identifier && i + 2 < n &&
        stringize(body[i + 1]) == "!" && stringize(body[i + 2]) == "(") {
      out += body[i]; out += body[i + 1]; out += body[i + 2];
      ++parens;
      stack.push_back({.paren_depth = parens});
      i += 2;
      continue;
    }
    if (s == "break" && stack.empty()) {
      out += on_break;
      used_break = true;
      continue;
    }
    out += body[i];
    if (s == "{") ++braces;
    else if (s == "(") ++parens;
    else if (s == "}") {
      end_frames([&](frame const& f) { return f.brace_depth == braces; });
      --braces;
    } else if (s == ")") {
      end_frames([&](frame const& f) { return f.paren_depth == parens; });
      --parens;
    } else if (s == ";") {
      end_frames([&](frame const& f) {
        return f.until_semi && f.braces == braces && f.parens == parens;
      });
    }
  }
  return out;
}

// A view's loop with the consumer pasted in: each 'co_yield E;' becomes
//
//   { <elem> = E; do { <body> } while (false); }
//
// where a top-level continue in the body leaves the do: on to the view's next
// element, wherever that comes from. (Not a label: a pasted body may be
// pasted again, at each co_yield of an inner view.) A top-level break jumps
// past the whole loop. Only the view's tokens are scanned for co_yield (a
// co_yield of the consumer's own is left alone).
consteval auto paste_yields(token_sequence loop, token_sequence elem,
                            token_sequence body) -> token_sequence {
  auto done = unique_id("done");
  bool used_break = false;
  token_sequence b = rewrite_breaks(body, ^^{ goto $done }, used_break);
  token_sequence out;
  std::size_t size = loop.size();
  for (std::size_t i = 0; i != size; ++i) {
    if (stringize(loop[i]) != "co_yield") {
      out += loop[i];
      continue;
    }
    std::size_t j = i + 1;
    for (int d = 0; j != size; ++j) {
      auto t = stringize(loop[j]);
      if (t == "(" || t == "[" || t == "{") ++d;
      else if (t == ")" || t == "]" || t == "}") --d;
      else if (t == ";" && d == 0) break;
    }
    token_sequence value = loop.subseq(i + 1, j - i - 1);
    out += ^^{ { $elem = $value; do { $b } while (false); } };
    i = j;  // past the ';'
  }
  if (used_break)
    return ^^{ { $out } $done:; };
  return out;
}

// iter::each_dir!(dir, int i, range, body;) -- iterate 'range' in direction
// 'dir', the way it says to (loop_traits).
template <class R>
__macro each_dir(direction dir, token_sequence elem, R&& range,
                 token_sequence body) {
  auto r = unique_id("r");
  return ^^{
    {
      auto&& $r = $range;
      ${loop_for(remove_cvref(^^R), ^^{ static_cast<decltype($r)&&>($r) },
                 extract<direction>(constant_of(dir)), elem, body)}
    }
  };
}
} // namespace iter

// each!(int i, range, body;) -- the loop that suits the range, with the body
// in it (so break, continue, and return mean what they would in a for loop).
template <class R>
__macro each(std::meta::token_sequence elem, R&& range,
             std::meta::token_sequence body) {
  return ^^{ iter::each_dir!(iter::direction::forward, $elem, $range, $body) };
}

// ------------------------------------------- the standard views' loops ----

namespace iter {
// ref_view and owning_view: their base, as is.
struct loop_of_base {
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    auto e = unique_id("e");
    return ^^{ iter::each_dir!($dir, auto&& $e, $self.base(), co_yield $e;); };
  }
};
template <class V>
struct loop_traits<std::ranges::ref_view<V>> : loop_of_base {};
template <class V>
struct loop_traits<std::ranges::owning_view<V>> : loop_of_base {};

template <class V>
struct loop_traits<std::ranges::reverse_view<V>> {
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    auto e = unique_id("e");
    return ^^{ iter::each_dir!(${!dir}, auto&& $e, $self.base(), co_yield $e;); };
  }
};

template <class V, class P>
struct loop_traits<std::ranges::filter_view<V, P>> {
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    auto p = unique_id("p"), e = unique_id("e");
    return ^^{
      auto&& $p = $self.pred();
      iter::each_dir!($dir, auto&& $e, $self.base(), if ($p($e)) co_yield $e;);
    };
  }
};

template <class V, class F>
struct loop_traits<std::ranges::transform_view<V, F>> {
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    // The function isn't exposed: it's a private (movable-box) member.
    info fn = *lookup(^^std::ranges::transform_view<V, F>, "__func_",
                      access_context::unchecked());
    auto f = unique_id("f"), e = unique_id("e");
    return ^^{
      auto&& $f = *$self.$fn;
      iter::each_dir!($dir, auto&& $e, $self.base(), co_yield $f($e););
    };
  }
};

template <class V, class P>
struct loop_traits<std::ranges::take_while_view<V, P>> {
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    auto p = unique_id("p"), e = unique_id("e");
    if (dir == direction::forward)
      return ^^{
        auto&& $p = $self.pred();
        iter::each_dir!($dir, auto&& $e, $self.base(),
                        if (!$p($e)) break; co_yield $e;);
      };
    // Backward: first find where it stops, then iterate that subrange.
    auto full = unique_id("full"), r = unique_id("r");
    return ^^{
      auto&& $p = $self.pred();
      auto&& $full = $self.base();
      auto $r = std::ranges::subrange(std::ranges::begin($full),
                                      std::ranges::find_if_not($full, $p));
      iter::each_dir!($dir, auto&& $e, $r, co_yield $e;);
    };
  }
};

// The inner ranges, each in the same direction.
template <class V>
struct loop_traits<std::ranges::join_view<V>> {
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    auto inner = unique_id("inner"), e = unique_id("e");
    return ^^{
      iter::each_dir!($dir, auto&& $inner, $self.base(),
                      iter::each_dir!($dir, auto&& $e, $inner, co_yield $e;););
    };
  }
};

// One loop per range, in order (backward: last to first).
template <class... Vs>
struct loop_traits<std::ranges::concat_view<Vs...>> {
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    // The ranges aren't exposed: they're a private tuple.
    info views = *lookup(^^std::ranges::concat_view<Vs...>, "__views_",
                         access_context::unchecked());
    token_sequence out;
    constexpr int n = sizeof...(Vs);
    for (int k = 0; k != n; ++k) {
      int i = dir == direction::forward ? k : n - 1 - k;
      auto e = unique_id("e");
      out += ^^{
        iter::each_dir!($dir, auto&& $e, std::get<$i>($self.$views),
                        co_yield $e;);
      };
    }
    return out;
  }
};
} // namespace iter

// ----------------------------------------------------------- the counts ----

int lt5_calls, even_calls, square_calls;
auto lt5 = [](int i) { ++lt5_calls; return i < 5; };
auto even = [](int i) { ++even_calls; return i % 2 == 0; };
auto square = [](int i) { ++square_calls; return i * i; };
void reset() { lt5_calls = even_calls = square_calls = 0; }

void peeled() {
  using namespace std::views;
  auto v = std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9};
  std::string got;

  // The iterator loop, for comparison.
  reset();
  for (int i : v | take_while(lt5) | filter(even) | reverse)
    got += std::to_string(i) + " ";
  assert(got == "4 2 " && lt5_calls == 8 && even_calls == 10);

  reset(); got.clear();
  each!(int i, v | take_while(lt5) | filter(even) | reverse,
        got += std::to_string(i) + " ";);
  assert(got == "4 2 " && lt5_calls == 5 && even_calls == 4);

  reset(); got.clear();
  each!(int i, v | take_while(lt5) | transform(square) | filter(even) | reverse,
        got += std::to_string(i) + " ";);
  assert(got == "16 4 " && lt5_calls == 5 && square_calls == 4 &&
         even_calls == 4);

  // Mutating through filter | reverse: one pass, so the predicate sees each
  // element once, before the body changes it.
  auto w = std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  each!(int& i, w | filter([](int x) { return x % 2 == 0; }) | reverse,
        i += 1;);
  assert((w == std::vector<int>{1, 3, 3, 5, 5, 7, 7, 9, 9, 11}));
}

void jumps() {
  auto v = std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9};
  std::string got;
  each!(int i, v | std::views::filter(even),
        if (i == 4) continue; if (i == 8) break; got += std::to_string(i) + " ";);
  assert(got == "2 6 ");

  auto first_over = [&](int n) -> int {
    each!(int i, v | std::views::transform(square), if (i > n) return i;);
    return -1;
  };
  assert(first_over(20) == 25);
  assert(first_over(100) == -1);
}

// join and concat: the body at several places, inside several loops. Each
// loop must do what the iterator loop does.
#define SAME_AS_FOR(DECL, RANGE, ...)                                          \
  do {                                                                         \
    auto via_each = [&] {                                                      \
      std::string got;                                                         \
      each!(DECL, RANGE, __VA_ARGS__);                                         \
      return got;                                                              \
    };                                                                         \
    auto via_for = [&] {                                                       \
      std::string got;                                                         \
      for (DECL : RANGE) {                                                     \
        __VA_ARGS__                                                            \
      }                                                                        \
      return got;                                                              \
    };                                                                         \
    assert(via_each() == via_for());                                           \
  } while (false)

void join_and_concat() {
  using namespace std::views;
  auto vv = std::vector<std::vector<int>>{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  auto v = std::vector<int>{1, 2, 3}, w = std::vector<int>{4, 5},
       x = std::vector<int>{6, 7, 8};
  auto not2 = [](int i) { return i != 2; };

  SAME_AS_FOR(int i, vv | join,
              if (i == 2) continue; if (i == 7) break; got += std::to_string(i););
  SAME_AS_FOR(int i, vv | join | reverse,
              if (i == 8) continue; if (i == 3) break; got += std::to_string(i););
  SAME_AS_FOR(int i, vv | join | filter(not2),
              if (i == 4) continue; got += std::to_string(i););
  SAME_AS_FOR(int i, concat(v, w, x),
              if (i == 2) continue; if (i == 7) break; got += std::to_string(i););
  SAME_AS_FOR(int i, concat(v, w, x) | reverse,
              if (i == 5) continue; if (i == 2) break; got += std::to_string(i););
  SAME_AS_FOR(int i, concat(v | filter(not2), w | reverse, vv | join),
              if (i == 4) continue; if (i == 8) break; got += std::to_string(i););

  // Inner ranges that are prvalues.
  auto blocks = iota(1, 4) | transform([](int n) { return std::vector<int>(n, n); });
  SAME_AS_FOR(int i, blocks | join, if (i == 2) continue; got += std::to_string(i););

  // One predicate call per element.
  reset();
  each!(int i, vv | join | filter(even), (void)i;);
  assert(even_calls == 9);

  // Mutation, through both.
  each!(int& i, concat(v, w), i *= 10;);
  each!(int& i, vv | join | reverse, i += 1;);
  assert((v == std::vector<int>{10, 20, 30} && w == std::vector<int>{40, 50}));
  assert((vv == std::vector<std::vector<int>>{{2, 3, 4}, {5, 6, 7}, {8, 9, 10}}));
}

// ------------------------------------------------------------ push-style ----

using std::meta::token_sequence;
using iter::direction;

template <class V, class P>
struct my_filter {
  V base;
  P pred;
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    auto e = std::meta::unique_id("e");
    return ^^{
      iter::each_dir!($dir, auto&& $e, $self.base,
                      if ($self.pred($e)) co_yield $e;);
    };
  }
};
template <class V, class P>
my_filter(V, P) -> my_filter<V, P>;

template <class V>
struct my_join {  // the body sits inside two loops
  V base;
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    auto inner = std::meta::unique_id("inner"), e = std::meta::unique_id("e");
    return ^^{
      iter::each_dir!($dir, auto&& $inner, $self.base,
                      iter::each_dir!($dir, auto&& $e, $inner, co_yield $e;););
    };
  }
};
template <class V>
my_join(V) -> my_join<V>;

template <class A, class B>
struct my_concat {  // the body is pasted at two co_yields
  A a;
  B b;
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    auto e = std::meta::unique_id("e");
    auto first = ^^{ $self.a }, second = ^^{ $self.b };
    if (dir == direction::reverse)
      std::swap(first, second);
    return ^^{
      iter::each_dir!($dir, auto&& $e, $first, co_yield $e;);
      iter::each_dir!($dir, auto&& $e, $second, co_yield $e;);
    };
  }
};
template <class A, class B>
my_concat(A, B) -> my_concat<A, B>;

template <class V>
struct my_reverse {
  V base;
  static consteval auto loop(token_sequence self, direction dir)
      -> token_sequence {
    auto e = std::meta::unique_id("e");
    return ^^{ iter::each_dir!(${!dir}, auto&& $e, $self.base, co_yield $e;); };
  }
};
template <class V>
my_reverse(V) -> my_reverse<V>;

void push_style() {
  auto v = std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9};
  std::string got;

  // Composed with standard views.
  reset();
  each!(int i,
        my_filter{v | std::views::transform([](int x) { return x * 3; }), even},
        got += std::to_string(i) + " ";);
  assert(got == "6 12 18 24 " && even_calls == 9);

  reset(); got.clear();
  each!(int i, my_filter{v | std::views::take_while(lt5), even},
        got += std::to_string(i) + " ";);
  assert(got == "2 4 " && lt5_calls == 5 && even_calls == 4);

  // A push-style view of a push-style view.
  got.clear();
  each!(int i, my_filter{my_filter{std::views::all(v), even},
                         [](int x) { return x > 4; }},
        got += std::to_string(i) + " ";);
  assert(got == "6 8 ");

  // break and continue leave or skip the consumer's iteration, from inside
  // both of join's loops.
  auto vv = std::vector<std::vector<int>>{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  got.clear();
  each!(int i, my_join{std::views::all(vv)},
        if (i == 2) continue; if (i == 7) break; got += std::to_string(i) + " ";);
  assert(got == "1 3 4 5 6 ");

  auto w = std::vector<int>{10, 20};
  got.clear();
  each!(int i, my_concat{std::views::all(v), std::views::all(w)},
        if (i % 2) continue; got += std::to_string(i) + " ";);
  assert(got == "2 4 6 8 10 20 ");

  // The body's own loops, and an each! in it, keep their break.
  got.clear();
  each!(int i, my_filter{std::views::all(v), even},
        for (int k = 0; k < 10; ++k) { if (k == 1) break; got += "k"; }
        each!(int j, std::views::iota(0, 5), if (j == 2) break; got += "j";);
        got += std::to_string(i) + " ";);
  assert(got == "kjj2 kjj4 kjj6 kjj8 ");

  // Reversed: a push-style view (with break and continue), and over a
  // standard view.
  got.clear();
  each!(int i, my_reverse{my_join{std::views::all(vv)}},
        if (i == 8) continue; if (i == 3) break; got += std::to_string(i) + " ";);
  assert(got == "9 7 6 5 4 ");

  got.clear();
  each!(int i, my_reverse{my_concat{std::views::all(v), std::views::all(w)}},
        got += std::to_string(i) + " ";);
  assert(got == "20 10 9 8 7 6 5 4 3 2 1 ");

  reset(); got.clear();
  each!(int i, my_reverse{v | std::views::take_while(lt5) | std::views::filter(even)},
        got += std::to_string(i) + " ";);
  assert(got == "4 2 " && lt5_calls == 5 && even_calls == 4);

  // The view's element is a unique_id: the consumer's 'e' is its own.
  int e = 100;
  got.clear();
  each!(int i, my_filter{std::views::all(v), even},
        got += std::to_string(i + e) + " ";);
  assert(got == "102 104 106 108 ");
}

// The consumer's own co_yield, in a coroutine: the body is pasted as is.
struct generator {
  struct promise_type {
    int value;
    generator get_return_object() {
      return generator{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(int v) {
      value = v;
      return {};
    }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
  std::coroutine_handle<promise_type> h;
  ~generator() { h.destroy(); }
  bool next() {
    h.resume();
    return !h.done();
  }
  int value() const { return h.promise().value; }
};

generator squares_of_evens(std::vector<int> const& v) {
  each!(int i, my_filter{std::views::all(v), even}, co_yield i * i;);
}

void coroutine() {
  auto v = std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9};
  std::string got;
  auto g = squares_of_evens(v);
  while (g.next())
    got += std::to_string(g.value()) + " ";
  assert(got == "4 16 36 64 ");
}

int main(int, char**) {
  peeled();
  jumps();
  join_and_concat();
  push_style();
  coroutine();
  return 0;
}
