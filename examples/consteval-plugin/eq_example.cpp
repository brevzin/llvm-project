// Copyright 2026 Jump Trading LLC
//
// Consumer TU using [[=Eq]] via a native consteval plugin (ZERO #include <meta>).

#include "eq.hpp"

struct [[=Eq]] P {
    int x;
    int y;
};
static_assert(P{1, 2} == P{1, 2});
static_assert(P{1, 2} != P{1, 3});

struct [[=Eq]] D : P {
    int z;
};
static_assert(D{{1, 2}, 3} == D{{1, 2}, 3});
static_assert(D{{1, 2}, 3} != D{{1, 2}, 4});
static_assert(D{{1, 2}, 3} != D{{1, 3}, 3});

template <class T>
struct [[=Eq]] Wrap {
    T t;
};

struct NotComparable {};

static_assert(EqComparable<Wrap<int>>);
static_assert(!EqComparable<NotComparable>);
static_assert(!EqComparable<Wrap<NotComparable>>);

int main() {}
