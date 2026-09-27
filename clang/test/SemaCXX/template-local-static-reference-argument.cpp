// RUN: %clang_cc1 -std=c++23 -verify %s
// Copyright 2026 Jump Trading LLC
// expected-no-diagnostics

struct Call { int value; };
template <const Call &C> constexpr const Call *address() { return &C; }
template <const Call &C, const Call &D = C, class... T>
constexpr const Call *with_default(T...) { return &D; }
template <const Call &...C> constexpr bool pack(const Call *p) {
  return ((&C == p) && ...);
}
struct Reader {
  template <const Call &C> constexpr const Call *get() { return &C; }
};
template <class T> struct TypedReader {
  template <const Call &C> constexpr const Call *get() { return &C; }
};

// Keep the definition-time overload choice: substitution changes the object
// identity in the selected specialization, not the overload set.
template <const Call &C> constexpr const Call *overload(int) { return &C; }
template <const Call &C> constexpr const Call *overload(...) { return nullptr; }

template <int N> constexpr bool test() {
  static constexpr Call call{4};
  auto fn = &address<call>;
  Reader r;
  TypedReader<int> tr;
  return address<call>() == &call && fn() == &call &&
         with_default<call>(1, 2.0) == &call && pack<call, call>(&call) &&
         r.get<call>() == &call && tr.get<call>() == &call &&
         overload<call>(0) == &call;
}
static_assert(test<0>());
static_assert(test<1>());

// A specialization's local static must remain distinct from other ones.
template <int N> constexpr const Call *local_address() {
  static constexpr Call call{4};
  return address<call>();
}
static_assert(local_address<0>() != local_address<1>());
static_assert(local_address<0>() == local_address<0>());

// Do not pessimistically make by-value arguments dependent just because the
// expression names a local static in a template.
template <int N> constexpr int value() { return N; }
template <int> void by_value() {
  static constexpr int n = 4;
  static_assert(value<n>() == 4);
}
