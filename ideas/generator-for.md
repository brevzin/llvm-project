In P2881, Jonathan and I had this idea to try to add a more convenient (and, more importantly, more *efficient*) appraoch to looping.

There are, I think, two kinds of approaches to internal interation. One is turning the body of the for loop into a callable. In D, `opApply` takes a callable that returns an `int` such a non-zero value means stop (in C++, we'd probably return an enum or something):

```cpp
template <class R, class P>
struct filter_view {
    R base_;
    P pred_;

    auto operator for(auto f) const -> int {
        for (auto&& e : base_) {
            if (pred_(e)) {
                if (f(fwd!(e))) {
                    return 1;
                }
            }
        }

        return 0;
    }
};
```

Here, a loop body that does `break` would translate into a callable which returns non-zero.

An alternative approach is to be able to spell this as something like a coroutine, except not actually use the C++20 coroutine machinery:

```cpp
template <class R, class P>
struct filter_view {
    R base_;
    P pred_;

    auto operator for() const -> {
        for (auto&& e : base_) {
            if (pred_(e)) {
                co_yield fwd!(e);
            }
        }
    }
};
```

Either approach is significantly easier to implement than iterators, and can be (especially for `filter`), significantly more performant and optimizable. The `co_yield` approach fits with the fundamentals better, you're pushing elements out (whereas iterators pull them), so it's nice for the code to do that.

I don't know how to reason about the fake coroutine approach, although it's probably more user-friendly though potentially harder to compose — the `opApply` approach you can just pass `f` to other places, but there's no `co_yield from` like Python has.

The `opApply` approach probably fundamnetally struggles with coroutines though, in a way that the fake coroutine approach doesn't — for instance a loop like:

```cpp
for (auto x : r) {
    co_yield f(x);
}
```

What is the callable that you pass to `r.operator for()`?

I also wrote an insane token sequence approach to this in https://brevzin.github.io/c++/2025/04/03/token-sequence-for/, which I think at the very least is worth revisiting as an example in our current implementation to see what it looks like.
