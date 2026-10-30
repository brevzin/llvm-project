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
// ADDITIONAL_COMPILE_FLAGS: -foperator-for -std=c++2d

// -foperator-for: a range-based for loop over a type with an 'operator for'
// -- a member, or a non-member found by argument-dependent lookup, as begin
// and end are -- expands it in place, with the loop body as its '__stmt'
// parameter: the body goes wherever 'operator for' inserts it ('body(e)'),
// with the loop variable initialized from the argument. The body is parsed
// where it's written, so 'break' leaves the loop, 'continue' ends this
// insertion, and 'return' and 'co_yield' are the enclosing function's --
// however many loops of its own 'operator for' puts around the insertion.
//
// Backward is a type, not syntax: a loop over std::ranges::backward(r) is a
// loop over backward_t<R>, whose 'operator for' a view provides as a hidden
// friend; for any other range, std::ranges' generic one uses its iterators,
// backward (from where ranges::next finds the end, as reverse_view does).
// reverse_view swaps the two -- as Rust's Rev swaps try_fold and try_rfold.
//
// The standard views' loops are 'operator for's: filter, transform,
// take_while, reverse, join, and concat become loops over the underlying
// ranges, calling each function only as often as needed (as with each!, in
// each-loop.pass.cpp, which does this with token sequences).

#include <array>
#include <cassert>
#include <coroutine>
#include <exception>
#include <list>
#include <ranges>
#include <string>
#include <type_traits>
#include <concepts>
#include <tuple>
#include <vector>

int lt5_calls, even_calls, square_calls;
auto lt5 = [](int i) { ++lt5_calls; return i < 5; };
auto even = [](int i) { ++even_calls; return i % 2 == 0; };
auto square = [](int i) { ++square_calls; return i * i; };
void reset() { lt5_calls = even_calls = square_calls = 0; }

// What the iterators do, for comparison: the same body in a loop over them.
#define SAME_AS_ITERATORS(DECL, RANGE, ...)                                    \
  do {                                                                         \
    auto via_operator_for = [&] {                                              \
      std::string got;                                                         \
      for (DECL : RANGE) {                                                     \
        __VA_ARGS__                                                            \
      }                                                                        \
      return got;                                                              \
    };                                                                         \
    auto via_iterators = [&] {                                                 \
      std::string got;                                                         \
      auto&& r = RANGE;                                                        \
      for (auto it = std::ranges::begin(r); it != std::ranges::end(r); ++it) { \
        DECL = *it;                                                            \
        __VA_ARGS__                                                            \
      }                                                                        \
      return got;                                                              \
    };                                                                         \
    assert(via_operator_for() == via_iterators());                             \
  } while (false)

// ------------------------------------------------------- standard views ----

void standard_views() {
  using namespace std::views;
  auto v = std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9};
  std::string got;

  // The iterators call the predicates 8 and 10 times; the loop, 5 and 4.
  reset();
  {
    auto&& r = v | take_while(lt5) | filter(even) | reverse;
    for (auto it = r.begin(); it != r.end(); ++it)
      got += std::to_string(*it) + " ";
  }
  assert(got == "4 2 " && lt5_calls == 8 && even_calls == 10);

  reset(); got.clear();
  for (int i : v | take_while(lt5) | filter(even) | reverse)
    got += std::to_string(i) + " ";
  assert(got == "4 2 " && lt5_calls == 5 && even_calls == 4);

  reset(); got.clear();
  for (int i : v | take_while(lt5) | transform(square) | filter(even) | reverse)
    got += std::to_string(i) + " ";
  assert(got == "16 4 " && lt5_calls == 5 && square_calls == 4 && even_calls == 4);

  // Mutating through filter | reverse: one pass.
  auto w = std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (int& i : w | filter([](int x) { return x % 2 == 0; }) | reverse)
    i += 1;
  assert((w == std::vector<int>{1, 3, 3, 5, 5, 7, 7, 9, 9, 11}));

  // An element's value category is kept.
  got.clear();
  for (int&& i : iota(0, 4))
    got += std::to_string(i);
  assert(got == "0123");

  // Ranges without an 'operator for', backward: a vector, arrays, and one
  // whose end is a sentinel.
  got.clear();
  for (int i : std::ranges::backward(v))
    got += std::to_string(i);
  assert(got == "987654321");

  int c[] = {1, 2, 3, 4, 5, 6};
  std::array<int, 6> a = {1, 2, 3, 4, 5, 6};
  got.clear();
  for (int i : std::ranges::backward(c))
    got += std::to_string(i);
  for (int i : std::ranges::backward(a))
    got += std::to_string(i);
  assert(got == "654321654321");
  got.clear();
  for (int i : c | filter(even) | reverse)
    got += std::to_string(i);
  for (int i : a | filter(even) | reverse)
    got += std::to_string(i);
  assert(got == "642642");

  std::list<int> l = {1, 2, 3, 4, 5, 6};
  auto first5 = l | take(5);  // counted_iterator, default_sentinel_t
  static_assert(!std::ranges::common_range<decltype(first5)>);
  got.clear();
  for (int i : std::ranges::backward(first5))
    got += std::to_string(i);
  assert(got == "54321");
  reset(); got.clear();
  for (int i : first5 | filter(even) | reverse)
    got += std::to_string(i);
  assert(got == "42" && even_calls == 5);
}

void jumps() {
  auto v = std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9};
  std::string got;
  for (int i : v | std::views::filter(even)) {
    if (i == 4) continue;
    if (i == 8) break;
    got += std::to_string(i) + " ";
  }
  assert(got == "2 6 ");

  auto first_over = [&](int n) -> int {
    for (int i : v | std::views::transform(square))
      if (i > n)
        return i;
    return -1;
  };
  assert(first_over(20) == 25);
  assert(first_over(100) == -1);

  // The same view type in the body: an 'operator for' expanded inside its own
  // expansion.
  int pairs = 0;
  for (int i : v | std::views::filter(even))
    for (int j : v | std::views::filter(even)) {
      if (j > i) break;
      ++pairs;
    }
  assert(pairs == 1 + 2 + 3 + 4);
}

void join_and_concat() {
  using namespace std::views;
  auto vv = std::vector<std::vector<int>>{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  auto a = std::vector<int>{1, 2, 3}, b = std::vector<int>{4, 5},
       c = std::vector<int>{6, 7, 8};
  auto not2 = [](int i) { return i != 2; };

  SAME_AS_ITERATORS(int i, vv | join,
                    if (i == 2) continue; if (i == 7) break; got += std::to_string(i););
  SAME_AS_ITERATORS(int i, vv | join | reverse,
                    if (i == 8) continue; if (i == 3) break; got += std::to_string(i););
  SAME_AS_ITERATORS(int i, vv | join | filter(not2),
                    if (i == 4) continue; got += std::to_string(i););
  SAME_AS_ITERATORS(int i, concat(a, b, c),
                    if (i == 2) continue; if (i == 7) break; got += std::to_string(i););
  SAME_AS_ITERATORS(int i, concat(a, b, c) | reverse,
                    if (i == 5) continue; if (i == 2) break; got += std::to_string(i););
  SAME_AS_ITERATORS(int i, concat(a | filter(not2), b | reverse, vv | join),
                    if (i == 4) continue; if (i == 8) break; got += std::to_string(i););

  auto blocks = iota(1, 4) | transform([](int n) { return std::vector<int>(n, n); });
  SAME_AS_ITERATORS(int i, blocks | join, if (i == 2) continue; got += std::to_string(i););

  reset();
  for (int i : vv | join | filter(even))
    (void)i;
  assert(even_calls == 9);

  for (int& i : concat(a, b))
    i *= 10;
  for (int& i : vv | join | reverse)
    i += 1;
  assert((a == std::vector<int>{10, 20, 30} && b == std::vector<int>{40, 50}));
  assert((vv == std::vector<std::vector<int>>{{2, 3, 4}, {5, 6, 7}, {8, 9, 10}}));
}

// --------------------------------------------------------- user views ----

// What a user writes is the same thing: a forward loop, and, for a view
// that can do better than its iterators backward, a backward one.
template <class V, class P>
struct my_filter {
  V base;
  P pred;
  void operator for(this auto&& self, __stmt body) {
    for (auto&& e : self.base)  // 'e' is ours: the body can't see it
      if (self.pred(e))
        body(e);
  }
  template <class Self>
    requires std::same_as<std::remove_cvref_t<Self>, my_filter>
  friend void operator for(std::ranges::backward_t<Self> b, __stmt body) {
    for (auto&& e : std::ranges::backward(b.range.base))
      if (b.range.pred(e))
        body(e);
  }
};
template <class V, class P>
my_filter(V, P) -> my_filter<V, P>;

template <class V>
struct my_join {
  V base;
  void operator for(this auto&& self, __stmt body) {
    for (auto&& inner : self.base)
      for (auto&& e : inner)
        body(e);  // a break in the body leaves both loops
  }
  template <class Self>
    requires std::same_as<std::remove_cvref_t<Self>, my_join>
  friend void operator for(std::ranges::backward_t<Self> b, __stmt body) {
    for (auto&& inner : std::ranges::backward(b.range.base))
      for (auto&& e : std::ranges::backward(inner))
        body(e);
  }
};
template <class V>
my_join(V) -> my_join<V>;

template <class A, class B>
struct my_concat {
  A a;
  B b;
  void operator for(this auto&& self, __stmt body) {
    for (auto&& e : self.a)
      body(e);
    for (auto&& e : self.b)
      body(e);
  }
  template <class Self>
    requires std::same_as<std::remove_cvref_t<Self>, my_concat>
  friend void operator for(std::ranges::backward_t<Self> r, __stmt body) {
    for (auto&& e : std::ranges::backward(r.range.b))
      body(e);
    for (auto&& e : std::ranges::backward(r.range.a))
      body(e);
  }
};
template <class A, class B>
my_concat(A, B) -> my_concat<A, B>;

template <class V>
struct my_reverse {
  V base;
  void operator for(this auto&& self, __stmt body) {
    for (auto&& e : std::ranges::backward(self.base))
      body(e);
  }
  template <class Self>
    requires std::same_as<std::remove_cvref_t<Self>, my_reverse>
  friend void operator for(std::ranges::backward_t<Self> b, __stmt body) {
    for (auto&& e : b.range.base)
      body(e);
  }
};
template <class V>
my_reverse(V) -> my_reverse<V>;

void user_views() {
  auto v = std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9};
  auto w = std::vector<int>{10, 20};
  auto vv = std::vector<std::vector<int>>{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  std::string got;

  // Composed with standard views, either way round.
  reset();
  for (int i : my_filter{v | std::views::transform([](int x) { return x * 3; }), even})
    got += std::to_string(i) + " ";
  assert(got == "6 12 18 24 " && even_calls == 9);

  reset(); got.clear();
  for (int i : my_filter{v | std::views::take_while(lt5), even})
    got += std::to_string(i) + " ";
  assert(got == "2 4 " && lt5_calls == 5 && even_calls == 4);

  got.clear();
  for (int i : my_filter{my_filter{v, even}, [](int x) { return x > 4; }})
    got += std::to_string(i) + " ";
  assert(got == "6 8 ");

  got.clear();
  for (int i : my_join{vv}) {
    if (i == 2) continue;
    if (i == 7) break;
    got += std::to_string(i) + " ";
  }
  assert(got == "1 3 4 5 6 ");

  got.clear();
  for (int i : my_concat{v, w}) {
    if (i % 2) continue;
    got += std::to_string(i) + " ";
  }
  assert(got == "2 4 6 8 10 20 ");

  got.clear();
  for (int i : my_reverse{my_join{vv}}) {
    if (i == 8) continue;
    if (i == 3) break;
    got += std::to_string(i) + " ";
  }
  assert(got == "9 7 6 5 4 ");

  got.clear();
  for (int i : my_reverse{my_concat{v, w}})
    got += std::to_string(i) + " ";
  assert(got == "20 10 9 8 7 6 5 4 3 2 1 ");

  // The body's own loops keep their break.
  got.clear();
  for (int i : my_filter{v, even}) {
    for (int k = 0; k < 10; ++k) {
      if (k == 1) break;
      got += "k";
    }
    for (int j : std::views::iota(0, 5)) {
      if (j == 2) break;
      got += "j";
    }
    got += std::to_string(i) + " ";
  }
  assert(got == "kjj2 kjj4 kjj6 kjj8 ");

  // The view's 'e' is its own: the body's 'e' is the caller's.
  int e = 100;
  got.clear();
  for (int i : my_filter{v, even})
    got += std::to_string(i + e) + " ";
  assert(got == "102 104 106 108 ");
}

// The body's own co_yield, in a coroutine: the body is the coroutine's code.
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
  for (int i : my_filter{my_join{std::vector<std::vector<int>>{v}}, even})
    co_yield i * i;
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
  standard_views();
  jumps();
  join_and_concat();
  user_views();
  coroutine();
  return 0;
}
