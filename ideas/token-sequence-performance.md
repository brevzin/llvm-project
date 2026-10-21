# Token sequences: compile-time performance

This collects the benchmarking done while deciding how `std::meta::token_sequence`
should be represented: the current model (a builtin, immutable token array
handle) against a model where `token_sequence` is `std::vector<token>` over a
scalar `std::meta::token`. It also records a related measurement of macro
expansion at parse time versus instantiation time.

The short version:

- Simulating the vector model with a stock `std::vector` suggested a **5–60×**
  slowdown on token-heavy code. Almost all of that turned out to be the cost of
  constant-evaluating libc++'s `std::vector`, not of tokens.
- A dedicated `vector<token>` specialization, with two native operations in the
  constant evaluator (fill from a literal, read back), brings token-heavy stress
  code to roughly **1–2×**.
- The **full implementation** of the vector model shows **no measurable
  difference** on every reflection test and example we have (−0.8% in total), and
  is even faster when scanning large bodies.
- The current model's real cost is elsewhere: building a sequence with `+=` in a
  loop uses **quadratic memory**, and iterating a `token_sequence` costs about
  25 µs per token.

## Contents

1. [Method](#method)
2. [Parse-time vs instantiation-time macro expansion](#parse-time-vs-instantiation-time-macro-expansion)
3. [The two models](#the-two-models)
4. [Stage 1: simulating with `std::vector`](#stage-1-simulating-with-stdvector)
5. [Why: constant-evaluating `std::vector` is slow](#why-constant-evaluating-stdvector-is-slow)
6. [Stage 2: a specialized vector](#stage-2-a-specialized-vector)
7. [Stage 3: where the remaining time goes](#stage-3-where-the-remaining-time-goes)
8. [Stage 4: native fill and join](#stage-4-native-fill-and-join)
9. [Memory: the current model's quadratic growth](#memory-the-current-models-quadratic-growth)
10. [Stage 5: the full implementation](#stage-5-the-full-implementation)
11. [Conclusions](#conclusions)
12. [Reproducing](#reproducing)

## Method

- **Machine:** 16 cores, otherwise idle during runs.
- **Compiler:** this fork, `RelWithDebInfo` with assertions enabled. Absolute
  times are therefore inflated, roughly 1.5× against a release build in the one
  comparison made (§5). Ratios are what matter.
- **Measurement:** wall-clock time of `-fsyntax-only` compiles. Implicit template
  instantiation and all constant evaluation still happen under `-fsyntax-only`.
- **Repetition:** the best of 2–3 runs per configuration.
- **Baselines:** unless noted, each table subtracts a baseline translation unit
  that includes the same headers and does nothing, so the numbers are the cost of
  the workload itself. Differences below about ±0.05 s are noise.
- **Microbenchmarks** loop a fixed operation inside `consteval` functions, many
  times, and divide. They're noisier at small scales (about ±10 µs per
  iteration).

## Parse-time vs instantiation-time macro expansion

The question was whether expanding an expression macro once at parse time,
rather than once per template instantiation, matters for compile time. `fwd!`
was used as the example.

`fwd!` as written (`template <class T> __macro fwd(T&& t)`) takes an expression
parameter. In a template, its argument is a type-dependent forwarding reference,
so expansion is deferred and the macro runs **once per instantiation**. A
raw-parameter version (`__macro fwd(token_sequence t)` expanding to
`static_cast<decltype(\(t))&&>(\(t))`) expands **at parse time**.

The workload is one function template using the macro K times, instantiated with
M distinct types. Seconds over a variant that uses no forwarding:

| K × M | `std::forward` | preprocessor `FWD` | raw `fwd!` (parse time) | `fwd!` (per instantiation) |
|---|---|---|---|---|
| 20 × 1 | ≈0 | ≈0 | ≈0 | ≈0 |
| 20 × 100 | +0.06 | +0.01 | +0.01 | +0.05 |
| 20 × 1000 | +0.55 | +0.05 | +0.05 | +0.51 |
| 100 × 1000 | +2.61 | +0.25 | +0.25 | +2.78 |

- **Parse-time expansion costs the same as the preprocessor macro:** about
  2.5 µs per use per instantiation. After the macro runs once, each instantiation
  pays only for the `static_cast` it produced.
- **Per-instantiation expansion costs the same as `std::forward`:** about 28 µs
  per use per instantiation. Each use redoes overload resolution for the macro,
  evaluates its body and parses its expansion.
- **It only matters at scale.** At 20 × 100 everything is noise. But avoiding
  `std::forward`'s compile-time cost is a large part of why people use a `FWD`
  macro at all.

Compile time isn't the only reason to prefer parse-time expansion:
- non-dependent names bind at the definition;
- errors appear when the template is defined;
- declarations the expansion introduces are visible to the rest of the template;
- tools see the expanded code.

Three adjustments to the deferral rules would let macros like `fwd!` and
`check!` expand at parse time (classification in the session notes):
- bind a type-dependent argument with a dependent type, and defer only if the
  body asks about that type;
- don't defer declaration macros in class templates unconditionally;
- treat `test_expression` on dependent code as a reason to defer.

## The two models

- **Current model.** `token_sequence` is a builtin type whose value is a pointer
  and a length into a token array allocated in the compilation's own bump
  allocator (`ASTContext`).
  - Copying one copies the handle.
  - A literal without interpolations reuses the array stored in its AST node.
  - Concatenation allocates a new array and copies both sides.
  - Nothing is ever freed.
  - To the constant evaluator it's a scalar: no lifetime, no allocation
    tracking, no destruction.
- **Vector model.** `token_sequence` is `std::vector<token>`, where `token` is a
  scalar builtin type. The vector is a real object in the program being
  evaluated:
  - its buffer is a constant-evaluation heap allocation the evaluator tracks;
  - it's mutable;
  - it's destroyed and freed at the end of its lifetime;
  - each element is a separate `APValue`.

The motivation for the vector model is ergonomic: scalar tokens (`id()`,
`str_lit()` and `identifier_of()` working on single tokens), and `vector`'s
familiar API in place of a range whose elements are the same type as the range.

## The workloads

Each workload exists in a version for each model, and every version checks its
results, so all models produce the same output:

| workload | what it does |
|---|---|
| **small** | 1000 invocations each of a `fwd!`-like macro and a `check!`-like macro that builds from two pieces |
| **enum** | `enum_to_string` for R enums of N enumerators (200 × 10, 20 × 100, 2 × 1000): appends one `case` per enumerator, then nests the result in two more literals |
| **split** | a `using_!`-style macro splitting a list of qualified names at commas, token by token (200 × 10 names, 20 × 100) |
| **scan** | a `λ!`-style macro scanning a body of L tokens for placeholders (200 × 30, 20 × 300) |
| **pass** | passing sequences through helper functions: 50 chains of 20 calls, each wrapping a ~20-token sequence, by value and by `const&` |

## Stage 1: simulating with `std::vector`

The first simulation needed no compiler changes beyond one stand-in:
- **Token:** a single-token `token_sequence`, a handle that's as cheap to copy as
  a scalar token would be.
- **Literal:** `std::vector<token>(std::from_range, ^^{…})`.
- **The compiler consuming a vector** (interpolation, a macro's result): an
  experimental `join` metafunction that concatenates the elements in one step.

| workload | current | `std::vector` | ratio |
|---|---|---|---|
| small × 1000 | 0.11 s | 2.41 s | 22× |
| enum 200 × 10 | 0.41 s | 3.67 s | 9× |
| enum 20 × 100 | 0.22 s | 2.93 s | 13× |
| enum 2 × 1000 | 0.29 s | 2.87 s ¹ | 10× |
| split 200 × 10 | 0.11 s | 3.00 s | 28× |
| split 20 × 100 | 0.11 s | 2.60 s | 23× |
| scan 200 × 30 | 0.15 s | 1.18 s | 8× |
| scan 20 × 300 | 0.12 s | 0.61 s | 5× |
| pass (`const&`) | 0.03 s | 1.88 s | 64× |
| pass (by value) | 0.07 s | 3.16 s | 49× |

¹ With the default limit, the vector version **exceeded clang's
constant-evaluation step limit**: building one 6,009-token vector in library code
is more than 1M steps. It needed `-fconstexpr-steps=200000000`. The current model
handles the same enum at the default limit.

## Why: constant-evaluating `std::vector` is slow

A microbenchmark of a 16-token literal, 20,000 evaluations:

| | per literal |
|---|---|
| `token_sequence` literal | ~6 µs |
| `std::vector` from the literal (`from_range`) | ~1.5 ms |
| `std::vector` from an initializer list of 16 tokens | ~1.1 ms |
| `std::vector<int>` of 16 `int`s | ~1.1 ms |
| `std::vector<info>` of 16 reflections | ~1.1 ms |
| `std::array<token, 16>` | ~10 µs |

The element type doesn't matter: a vector of `int`s costs the same. What costs
is constant-evaluating `std::vector`. Constructing a vector of N `int`s, 4,000
times:

| elements | per vector (fork + libc++) |
|---|---|
| 0 (no allocation) | ~25 µs |
| 1 | ~520 µs |
| 4 | ~640 µs |
| 16 | ~1,080 µs |
| 64 | ~2,800 µs |

That's about **500 µs per allocation plus about 36 µs per element**. It isn't
specific to the fork:

| compiler + library | per allocation | per element |
|---|---|---|
| fork (assertions) + libc++ | ~490 µs | ~36 µs |
| fork + libstdc++ | ~175 µs | ~25 µs |
| stock clang 22.1.3 (release) + libstdc++ | ~310 µs | ~20 µs |
| stock clang 22.1.3, `-fexperimental-new-constant-interpreter` | ~90 µs | ~5 µs |

The bytecode interpreter is 3–4× faster, but the fork's reflection doesn't run
under it (`<meta>` fails to compile), so it can't be measured end to end here.

## Stage 2: a specialized vector

Four implementations of the vector, in increasing order of specialization:

| implementation | how it works |
|---|---|
| libc++ `std::vector` | as shipped |
| **lean** | raw pointer, size, capacity; `std::allocator<T>::allocate` and `std::construct_at` directly; no `allocator_traits`, exception guards, sanitizer annotations or hardening |
| **scalar** | what a `vector<info>`/`vector<token>` specialization can do because both are scalar types: `new T[cap]` (default-initialization is free for scalars) and plain assignment, no `construct_at`/`destroy_at` |
| **bulk** | scalar, plus `__builtin_memcpy` for element-to-element copies (growth, copying, appending a vector). Clang constant-evaluates `memcpy` natively for trivially copyable types, including `token_sequence` and `info`. |

Microbenchmark, vectors of `int`, per construction:

| variant | per allocation | per element |
|---|---|---|
| libc++ `std::vector` | ~500 µs | ~37 µs |
| lean | ~110 µs | ~31 µs |
| bare `allocate` + `construct_at` loop | ~35 µs | ~14 µs |
| `new T[n]` + assignment | ~0 µs | ~5 µs |

- **libc++'s layers are mostly a fixed cost per allocation,** which the lean
  version removes.
- **Per element, the cost is calling `std::construct_at`** in the evaluator.
  Only the scalar trick (assignment instead of `construct_at`) gets under it.

End to end, seconds over baseline:

| workload | current | `std::vector` | lean | scalar | bulk |
|---|---|---|---|---|---|
| small × 1000 | 0.08 | 2.39 | 1.35 | 0.88 | 0.93 ² |
| enum 200 × 10 | 0.40 | 3.92 | 2.62 | 1.58 | — |
| enum 20 × 100 | 0.22 | 3.04 | 2.10 | 1.24 | 1.22 ² |
| enum 2 × 1000 | 0.22 | 2.87 | 2.06 | 1.26 | 1.29 ² |
| split 200 × 10 | 0.04 | 3.02 | 1.43 | 0.82 | 0.71 ² |
| split 20 × 100 | 0.11 | 2.51 | 1.37 | 0.77 | — |
| scan 200 × 30 | 0.16 | 1.13 | 0.92 | 0.57 | — |
| scan 20 × 300 | 0.14 | 0.63 | 0.68 | 0.74 | — |
| pass (`const&`) | 0.14 | 1.80 | 1.37 | 0.66 | 0.34 ² |
| pass (by value) | 0.01 | 3.18 | 2.49 | 0.87 | 0.43 ² |

² From a separate run, against scalar times of 0.96, 1.41, 1.31, 0.85, 0.69 and
0.88 s in that run.

The scalar specialization recovers about 2.5–3.5× of `std::vector`'s overhead.
Bulk copying halves the cost where copying dominates, and barely helps elsewhere.

## Stage 3: where the remaining time goes

`enum` 20 × 100 (12,000 tokens) on the bulk vector, against the current model's
0.18 s:

| variant | time | finding |
|---|---|---|
| as benchmarked | 1.14 s | — |
| nested literals interpolate the vector directly | 0.73 s | the simulation's vector → literal → vector round trips cost 43% of the gap; a real vector model interpolates natively, so this part was an artifact |
| also no temporary vectors | 0.71 s | temporaries are nearly free |

What remained, per token:
- **Iterating the literal's tokens: ~32 µs.**
- `push_back` into the vector: ~9 µs.
- Reading the vector back into a literal: ~5 µs.

**Iterating a `token_sequence` is the expensive part, in the current model
too.** A range-`for` makes three interpreted calls per token on the iterator
(`!=`, `*`, `++`), and `*` calls a metafunction that **allocates a new one-token
sequence for every element**. Measured directly, with the same comparison per
token:

| | per token |
|---|---|
| iterating a `token_sequence` | ~25 µs |
| iterating a vector by pointer | ~7 µs |
| converting a `token_sequence` to a vector (through its iterator) | ~55 µs |

So any token-processing macro in the current model (`using_!`, `call!`, `λ!`,
`check!`) pays about 25 µs per token visited. A model where a sequence's tokens
are a contiguous array of scalar tokens would iterate by pointer, about 4×
cheaper.

The best implementable vector so far (bulk, without the simulation's round
trips), seconds over baseline:

| workload | current | `std::vector` | best vector | best vs current |
|---|---|---|---|---|
| small × 1000 | 0.12 | 2.36 | 0.82 | 6.9× |
| enum 200 × 10 | 0.40 | 3.62 | 0.88 | 2.2× |
| enum 20 × 100 | 0.20 | 2.97 | 0.70 | 3.6× |
| enum 2 × 1000 | 0.23 | 2.83 | 0.64 | 2.7× |
| split 200 × 10 | 0.07 | 2.94 | 0.75 | ~11× |
| split 20 × 100 | 0.12 | 2.58 | 0.68 | 5.5× |
| scan 200 × 30 | 0.19 | 1.21 | 0.44 | 2.3× |
| scan 20 × 300 | 0.14 | 0.56 | 0.33 | 2.4× |
| pass (`const&`) | ≈0 | 1.87 | 0.29 | — |
| pass (by value) | ≈0 | 3.10 | 0.36 | — |

Most of what remained was turning a literal into a vector, which went through
the iterator at ~55 µs per token. A real vector model would have the compiler do
that natively.

## Stage 4: native fill and join

Two operations were added to the constant evaluator. They're intercepted in its
metafunction dispatch and work the way the evaluator already implements
`__builtin_memcpy`: a native loop over the allocation's `APValue` array, with
nothing interpreted.
- **`fill(token* dst, token_sequence src)`** writes a literal's tokens into an
  allocation in one step. This is what a compiler-built `vector<token>` literal
  does.
- **`join(const token* p, size_t n)`** concatenates an array of tokens. The
  compiler uses it to read a vector, for interpolation or as a macro's result.

`native_vector` is the bulk vector, constructing and appending from a literal
through `fill`. Seconds over baseline:

| workload | current | `std::vector` | bulk | native |
|---|---|---|---|---|
| small × 1000 | 0.15 | 2.32 | 0.77 | 0.47 |
| enum 200 × 10 | 0.41 | 3.47 | 0.87 | 0.67 |
| enum 20 × 100 | 0.24 | 2.77 | 0.68 | 0.44 |
| enum 2 × 1000 | 0.24 | 2.79 | 0.70 | 0.46 |
| split 200 × 10 | 0.11 | 2.95 | 0.66 | 0.40 |
| split 20 × 100 | 0.11 | 2.56 | 0.64 | 0.38 |
| scan 200 × 30 | 0.16 | 1.17 | 0.45 | 0.30 |
| scan 20 × 300 | 0.13 | 0.56 | 0.35 | 0.27 |
| pass (`const&`) | 0.02 | 1.80 | 0.31 | 0.30 |
| pass (by value) | 0.01 | 3.19 | 0.38 | 0.37 |

What a native vector still pays, which the handle model doesn't:
- **about 25–30 µs per vector literal** on top of a handle literal: an
  allocation, a deallocation, interpreted member calls;
- **about 5.5 µs per `push_back`**, each one an interpreted call;
- **an allocation per copy** (the pass-through rows).

## Memory: the current model's quadratic growth

The current model never frees a token array, and every `+=` copies the whole
sequence so far into a new one. Building N tokens one piece at a time therefore
allocates on the order of N² tokens in total. Peak memory for one enum's
`to_string`, above a 136 MB baseline:

| enumerators | current model | native vector |
|---|---|---|
| 250 | +5 MB | +8 MB |
| 500 | +19 MB | +9 MB |
| 1,000 | +73 MB | +10 MB |
| 2,000 | **+287 MB** | +13 MB |

Doubling the enum quadruples the current model's memory. The vector frees as it
goes and grows by doubling.

This is fixable without vector semantics: append in place when the left-hand
side is the newest allocation in the arena and nothing else refers to it, or
build into one growing, compiler-owned buffer (`list_builder` could be that) and
hand out an immutable `token_sequence` at the end.

## Stage 5: the full implementation

The vector model was then implemented fully (branch `token-vector-model`):
- `std::meta::token` is a builtin scalar type.
- The compiler's immutable token array remains as the internal representation
  (`std::meta::__token_array`), used by metafunctions and by literals inside
  `<meta>`.
- `std::meta::token_sequence` is an explicit specialization of
  `std::vector<token>` in `<meta>`, built like `native_vector`.
- Every `^^{…}` is a `token_sequence` once `<meta>` has declared one.
- Macro results, interpolation and `queue_injection` still take the internal
  array; a vector converts to it implicitly.
- A raw macro parameter of type `token_sequence` is an ordinary parameter
  object, constructed from the argument when the body is evaluated and
  destroyed at the end of the call. A raw pack's elements are
  `token_sequence`s too.

All 104 reflection tests and 6,026 clang tests pass. The benchmark compared:
- **current:** the previous compiler, saved before the change, on the original
  sources;
- **vector:** the new compiler on the migrated sources.

The migrations were small (`token tok` loop variables, `(^^{ x })[0]` where a
single token is wanted; details in the session notes).

### Real tests and examples

| | current | vector |
|---|---|---|
| 80 reflection tests compiled by both, total | 105.19 s | 104.36 s (−0.8%) |
| 14 token- and macro-heavy tests, above the `<meta>` cost | 10.62 s | 10.44 s (−2%) |
| `#include <meta>` alone | 0.632 s | 0.628 s |

Every individual test was within ±3%, in both directions. The ten compilable
examples (`bitmask`, `cached`, `eq`, `lam`, `macro`, `persist`, `trym`,
`type-erasure-constraints`, `vec`, `iterator-interface-owl`) were all within
noise. In these files, token work is a small part of compile time next to
`<meta>` and the rest of the reflection machinery.

### Stress workloads in each model's real API

The sources are nearly identical in both models, since `+=`, `==`,
interpolation and literals all still work; loop variables change from
`token_sequence` to `token`. Seconds over each model's own baseline:

| workload | current | vector | ratio |
|---|---|---|---|
| small × 1000 | 0.12 | 0.22 | 1.9× |
| enum 200 × 10 | 0.39 | 0.51 | 1.3× |
| enum 20 × 100 | 0.22 | 0.29 | 1.3× |
| enum 2 × 1000 | 0.27 | 0.28 | 1.0× |
| split 200 × 10 | 0.09 | 0.42 | 4.5× |
| split 20 × 100 | 0.10 | 0.41 | 4.2× |
| split, comparand hoisted, 200 × 10 | 0.11 | 0.27 | 2.5× |
| split, comparand hoisted, 20 × 100 | 0.12 | 0.24 | 2.0× |
| scan 200 × 30 | 0.16 | 0.19 | 1.2× |
| scan 20 × 300 | 0.11 | 0.07 | **0.6×** |
| pass (`const&`) | 0.02 | 0.09 | — |
| pass (by value) | 0.01 | 0.12 | — |

- **Generation** (enum) is within 1.0–1.3×.
- **Scanning a large body is faster** in the vector model: pointer iteration
  beats the `token_sequence` iterator.
- **Small macros** pay the fixed cost of a few vectors per invocation (1.9×).
- **Token-by-token building is the weak spot.** `split` compares every token
  against `^^{,}`. In the current model that literal is free; in the vector model
  it allocates a fresh vector on every iteration. Hoisting it brings 4× down to
  about 2×. The rest is the interpreted `push_back` behind `cur += t`.
- **Passing a sequence around costs an allocation per copy,** against nothing.

### Splitting the way each model would write it

`split` appends one token at a time (`cur += t`). That's natural in the current
model, where each `+=` is a native array copy, but not in the vector model,
where it's an interpreted `push_back`. Natural vector code finds each comma and
appends the whole run at once, as a contiguous subrange (one `memcpy`):

```cpp
__macro using_(token_sequence names) {
  token_sequence commas = ^^{ , }, prefix = ^^{ using }, semis = ^^{ ; };
  token comma = commas[0], semi = semis[0];
  token_sequence out;
  for (auto b = names.begin(), e = names.end(); b != e;) {
    auto it = b;
    while (it != e && !(*it == comma))    // or std::ranges::find(b, e, comma)
      ++it;
    out.append_range(prefix);
    out.append_range(std::ranges::subrange(b, it));
    out.push_back(semi);
    b = it == e ? e : it + 1;
  }
  return out;
}
```

The current model keeps its token-at-a-time version, since it has no cheap way
to take a sub-run of a sequence. Seconds over baseline:

| names × invocations | current `split` | vector `split` | vector, hoisted | vector, block + `ranges::find` | vector, block + loop |
|---|---|---|---|---|---|
| 10 × 200 | 0.12 | 0.43 | 0.27 | 0.43 | **0.23** (1.8×) |
| 100 × 20 | 0.12 | 0.40 | 0.23 | 0.39 | **0.19** (1.6×) |
| 1000 × 2 | 0.19 | 0.43 | 0.25 | 0.43 | **0.20** (1.0×) |

- **Block appending works**, but only with a hand-written search. Written that
  way, splitting is 1.0–1.8×, and the gap closes as inputs grow, since the
  current model's `out += …` copies all of `out` on every append.
- **`std::ranges::find` costs about as much as everything else combined.**
  libc++'s implementation goes through several layers (iterator unwrapping,
  projections, `std::invoke`, `equal_to`), and the evaluator interprets them all
  for every element. That cuts against one of the vector model's selling points,
  using standard algorithms. If adopted, `find` (and similar algorithms) over
  contiguous tokens would want a native fast path, as `memchr` has for
  characters.
- A raw parameter is one object for the whole body, so `names.begin()` and
  `names.end()` refer to the same vector and mutations persist. (An earlier
  version of the implementation built a fresh vector at each mention of a raw
  parameter, so the code needed `token_sequence ts = names;` first. That was a
  bug, now fixed. The numbers above were measured after the fix, and the fix
  barely changed the other workloads.)

## Stage 6: a token-literal view type (`token_view`)

A middle ground between the two models (branch `token-vector-model`, on top of
the full vector implementation):

- **`^^{ … }` is a `std::meta::token_view`:** a library class with a
  `const token*` and a size, viewing the literal's tokens in static storage
  (an unnamed global constant, like a string literal's array).
  - `begin()`/`end()` return pointers, and it has indexing, `size()` and so on.
  - It has `consteval operator token_sequence()` and `to_vec()`.
  - It's trivially copyable, borrowed and a view.
  - Users can't construct one; it's the same idea as `std::initializer_list`.
- **`token_sequence` is still `vector<token>`,** the builder.
- **Raw macro parameters** are `token_view` when the body only reads them, and
  `token_sequence` when it modifies them.
- **Interpolation** accepts any contiguous range of tokens (views, vectors,
  `std::span<const token>`).

Three things had to be fixed before the comparison was fair:
- **`token_view`'s operators** were first constrained templates in
  `std::meta`, found by argument-dependent lookup for every operator applied
  to any `std::meta` type (every `info == info`). That cost about 7% on tests
  with no tokens at all. They're now hidden friends, and the `token_sequence`
  operators take concrete types.
- **`<meta>`'s own non-template members** (`to_vec`, the operators,
  `list_builder`) instantiated `vector::append_range` and the `ranges`
  concepts in every translation unit, about 60 ms. They now use a plain
  `__append(ptr, n)`.
- **Literal storage was first deduplicated by value.** Literals that compare
  equal but differ (interpolated constants of different enum types) then
  shared one array, which was wrong. Each token array now gets its own
  storage, cached by the array's identity.

Literal costs (one literal of 16 tokens with an interpolation):

| | make | make + hand to the compiler |
|---|---|---|
| current handle | 3.5 µs | 7.3 µs |
| `vector<token>` | 38 µs | 92 µs |
| `token_view`, first version | 17.5 µs | 50 µs |
| `token_view` with native construction and read-back | 11.8 µs | 22.5 µs |
| plus tokens referring to their slot (no allocation per token) | **8.9 µs** | **18.3 µs** |

The native paths:
- constructing a `token_view` from the internal array is intercepted in the
  evaluator;
- converting a view back to the internal array is intercepted, and reads a
  side table mapping each materialized array to the token array it came from:
  O(1) for a whole view, a native slice otherwise.

Tests and examples (90 cases compiled by all three):

| | current | vector | view |
|---|---|---|---|
| total | 119.44 s | 119.24 s (−0.2%) | 120.00 s (+0.5%) |
| 14 token- and macro-heavy tests, above `<meta>` | 10.40 s | 10.47 s | 10.69 s |
| 10 examples | 14.72 s | 14.77 s | 14.89 s |

Stress workloads, seconds over baseline:

| workload | current | vector | view |
|---|---|---|---|
| small × 1000 | 0.11 | 0.23 (2.0×) | 0.22 (2.0×) |
| enum 200 × 10 | 0.40 | 0.52 | 0.49 |
| enum 20 × 100 | 0.22 | 0.29 | 0.27 |
| enum 2 × 1000 | 0.28 | 0.30 | 0.26 |
| split 200 × 10 | 0.10 | 0.42 (4.3×) | 0.32 (3.3×) |
| split 20 × 100 | 0.10 | 0.39 | 0.31 |
| split, hoisted, 200 × 10 | 0.10 | 0.25 | 0.29 |
| split, block + loop, 200 × 10 | 0.10 | 0.21 | 0.24 |
| split, block + loop, 20 × 100 | 0.10 | 0.17 | 0.19 |
| scan 200 × 30 | 0.15 | 0.15 | 0.17 |
| scan 20 × 300 | 0.12 | 0.08 | 0.10 |
| pass, `const&` (view model: `std::span<const token>`) | 0.02 | 0.07 | 0.11 |
| pass, by value (view model: `token_view`) | 0.02 | 0.13 | **0.05** |

- **On real code, all three models are the same.**
- **On stress code, the view model is about on par with the vector model.**
  - Literals made inside loops (`split` comparing against `^^{,}`) and passing
    views by value are better.
  - Spans are slower than views. Converting a view to `std::span` goes through
    `span`'s interpreted range constructor, and interpolating a span through a
    library call.
  - The rest is a few percent either way.
- **Building is identical:** the `vector`. Small macros that build (`check!`
  appends two pieces) cost the same as in the vector model. Written naturally
  for this model, as a single literal, they wouldn't build at all.
- **A literal is still ~2.5× a handle literal,** from materializing storage for
  every evaluation of a literal that contains interpolations.

### Lazy materialization

`token_view` then changed representation, but not API. It now holds the
compiler's token-array handle and its size, and gets static storage only when
it's asked for (`data()`, `begin()`/`end()`, indexing), made once per token
array and cached:
- making a literal is a handle and a size, built natively;
- copying one copies two scalars;
- handing it to the compiler returns the handle;
- `to_vec()` and concatenation fill from the handle natively;
- `==` is the built-in comparison of handles.

| one 16-token literal | make | make + hand to the compiler |
|---|---|---|
| current handle | 3.5 µs | 7.3 µs |
| `vector<token>` | 38 µs | 92 µs |
| eager `token_view` | 8.9 µs | 18.3 µs |
| **lazy `token_view`** | **2.3 µs** | **6.7 µs** |

Tests and examples (90 cases): current 120.93 s, vector 120.48 s, **view
121.20 s**. The 14 token-heavy tests above the `<meta>` cost: 10.81 s, 10.77 s,
11.05 s. Examples: 15.26 s, 14.90 s, 15.13 s.

Stress workloads, seconds over baseline:

| workload | current | vector | lazy view |
|---|---|---|---|
| small × 1000 | 0.13 | 0.24 (1.75×) | 0.22 (1.6×) |
| enum 200 × 10 | 0.41 | 0.53 | 0.49 (1.2×) |
| enum 20 × 100 | 0.22 | 0.32 | 0.26 (1.2×) |
| enum 2 × 1000 | 0.28 | 0.29 | 0.25 (0.9×) |
| split 200 × 10 | 0.10 | 0.44 (4.3×) | 0.31 (3.0×) |
| split 20 × 100 | 0.11 | 0.42 (4.0×) | 0.28 (2.6×) |
| split, hoisted, 200 × 10 | 0.10 | 0.27 | 0.30 |
| split, block + loop, 200 × 10 | 0.10 | 0.23 | 0.24 |
| split, block + loop, 20 × 100 | 0.12 | 0.20 | 0.20 |
| scan 200 × 30 | 0.16 | 0.17 | 0.17 |
| scan 20 × 300 | 0.13 | 0.09 | 0.10 |
| pass, `std::span<const token>` / `const&` | 0.01 | 0.09 | 0.11 |
| pass, `token_view` by value / by value | 0.03 | 0.13 | **0.02** |

- **Literals and passing views now cost what handles cost.** Passing views by
  value matches the current model, and the workloads dominated by literals
  improved (small 1.6×, enum 0.9–1.2×, token-at-a-time split 2.6–3.0×).
- **What remains is building,** which is identical in the vector and view
  models: `push_back` (interpreted), appending, and converting a vector back
  to the compiler. Small macros that build (`check!` appends two pieces) and
  splitting are those workloads.
- **Passing as `std::span` is slower than passing a view.** A span needs the
  view's storage (`data()`) and goes through `span`'s interpreted range
  constructor.

### Native read-back of a `token_sequence`

Interpolating a `token_sequence`, or returning one from a macro, converted it
to the compiler's token array through the vector's (interpreted) conversion
function and a `join` that read the heap buffer element by element, each
element through the evaluator's full lvalue lookup. Two changes:
- the vector's conversion is intercepted, as `token_view`'s is;
- reading a run of tokens out of a heap allocation copies them straight out
  of the allocation's array, in a plain C++ loop.

Interpolating a 16-token `token_sequence` went from **54.5 µs to 6.4 µs**.

Stress workloads after it, with doubled sizes and the best of 5 runs (seconds
over baseline):

| workload | current | vector | view | view / current |
|---|---|---|---|---|
| small × 2000 | 0.24 | 0.46 | 0.41 | 1.7× |
| enum 200 × 10 | 0.40 | 0.51 | 0.47 | 1.18× |
| enum 20 × 100 | 0.23 | 0.30 | 0.25 | 1.09× |
| enum 2 × 1000 | 0.28 | 0.29 | 0.24 | 0.86× |
| split 400 × 10 | 0.20 | 0.87 | 0.58 | 2.95× |
| split 40 × 100 | 0.23 | 0.81 | 0.53 | 2.35× |
| split, hoisted, 400 × 10 | 0.21 | 0.56 | 0.56 | 2.65× |
| split, block + loop, 400 × 10 | 0.21 | 0.41 | 0.47 | 2.2× |
| split, block + loop, 40 × 100 | 0.24 | 0.36 | 0.35 | 1.45× |
| scan 400 × 30 | 0.33 | 0.33 | 0.32 | 0.97× |
| scan 40 × 300 | 0.25 | 0.18 | 0.20 | 0.80× |
| pass, `std::span` / `const&`, 200 × 20 | 0.10 | 0.38 | 0.24 | 2.5× |
| pass, by value, 200 × 20 | 0.08 | 0.53 | 0.12 | 1.6× |

Tests and examples (90 cases) remained level: 119.84 s, 119.31 s, 120.00 s.

What remains above the current model is building one token at a time
(`cur += t`, an interpreted `push_back`, and resetting `cur` per name), small
macros that build a vector, and passing `std::span`.

### Native `push_back` and token operators

A native `push_back` alone barely moved the workloads: building one token at
a time was dominated by the interpreted call to the *operator* wrapping it.
Per token, in a loop of 64:

| operation | before | native `push_back` | native operators |
|---|---|---|---|
| `cur.push_back(t)` | (12.6 in the vector model) | 1.7 µs | 1.8 µs |
| `cur += t` | 7.1 µs | 7.1 µs | 2.0 µs |
| `t == ^^{,}` | 8.2 µs | 8.2 µs | 3.3 µs |
| `t == comma` (two tokens) | 0.4 µs | — | — |

These are now evaluated natively:
- **`push_back(token)`:** when there is room in the buffer, the token is stored
  straight into the heap array and the size bumped. A full buffer runs the
  function itself, which grows it, so that path is rare.
- **`+=` on a `token_sequence`** (with a token, a view, or another sequence).
  Once its operands are evaluated it must not fall back, since that would
  evaluate them twice. So a full buffer is grown by calling the class's own
  `__grow_for` from the evaluator.
- **`==` between tokens, views, and sequences:** the token arrays are compared
  as the built-in operator compares tokens.

Stress workloads after this, with doubled sizes and the best of 5 runs:

| workload | current | vector | view | view / current |
|---|---|---|---|---|
| small × 2000 | 0.24 | 0.47 | 0.43 | 1.76× |
| enum 200 × 10 | 0.43 | 0.55 | 0.48 | 1.10× |
| enum 20 × 100 | 0.23 | 0.32 | 0.24 | 1.06× |
| enum 2 × 1000 | 0.28 | 0.29 | 0.22 | 0.78× |
| split 400 × 10 | 0.20 | 0.88 | 0.44 | 2.24× |
| split 40 × 100 | 0.21 | 0.81 | 0.39 | 1.86× |
| split, hoisted, 400 × 10 | 0.20 | 0.53 | 0.45 | 2.27× |
| split, block + loop, 400 × 10 | 0.20 | 0.42 | 0.41 | 2.03× |
| split, block + loop, 40 × 100 | 0.21 | 0.34 | 0.33 | 1.57× |
| scan 400 × 30 | 0.31 | 0.33 | 0.32 | 1.05× |
| scan 40 × 300 | 0.26 | 0.17 | 0.18 | 0.68× |
| pass, `std::span` / `const&`, 200 × 20 | 0.05 | 0.36 | 0.23 | 4.6× |
| pass, by value, 200 × 20 | 0.07 | 0.51 | 0.09 | 1.4× |

Tests and examples (90 cases) remained level: 119.84 s, 119.33 s, 120.40 s.

### Memory

Peak RSS (`/usr/bin/time -f %M`, one run per case; `~/tmp/vwbench/mem.py`).

The tests and examples are the same in every model. Their total is
13,854 MB (current) and 13,897 MB (view), with a median ratio of 1.000; no
case differs by more than 5 MB.

Stress workloads, MB above a 148 MB baseline:

| workload | current | vector | view |
|---|---|---|---|
| small × 2000 | 5.4 | 6.5 | 5.3 |
| enum 200 × 10 | 6.0 | 6.4 | 5.0 |
| enum 20 × 100 | 16.5 | 4.3 | 3.0 |
| enum 2 × 1000 | **141.5** | 6.6 | 5.3 |
| split 400 × 10 | 6.5 | 5.0 | 5.0 |
| split 40 × 100 | 26.7 | 4.6 | 4.3 |
| split, block + loop, 40 × 100 | 26.7 | 5.6 | 6.3 |
| scan 400 × 30 | 6.2 | 7.2 | 8.8 |
| scan 40 × 300 | 3.1 | 3.8 | 5.3 |
| pass, `std::span` / `const&` | 7.9 | 20.6 | 30.4 |
| pass, by value | 7.9 | 20.7 | 8.0 |

The current model's memory grows with the *square* of a sequence built by
repeated `+=`. Every `+=` makes a new immutable sequence in the AST
context's arena, and that arena is never freed. Building a
1000-enumerator `enum_to_string` costs 141 MB.

A vector's buffer is evaluator heap, freed when the evaluation ends, so the
view model stays flat. Its outlier is passing `std::span` (+30 MB, not yet
investigated).

The pattern is that every interpreted call into `<meta>` costs about 5 µs,
whatever its body does. So the remaining gaps are the calls still left:
- small macros construct a `token_sequence` from a view;
- splitting resets `cur` by assigning to it;
- `std::span` has its constructor.

## Stage 7: a compiler-owned `token_sequence`

Review of the view model concluded that `token` as a scalar is right, but
the full `vector` interface is not: `token_sequence` should be a type built
for the job, owned by the compiler. This stage keeps what the measurements
favored from each model, and drops the `vector` interface.

**The type.** `^^{...}` has type `std::meta::token_sequence`. It is a class
with two members: the compiler's token array and its size. Every operation
is native.

| operation | how | cost |
|---|---|---|
| literal | holds the literal's array, no copy | as the lazy view |
| copy, pass by value | copy the handle | O(1) |
| `+=`, `+` | append in place when the sequence ends its buffer and there is room; otherwise copy into a new buffer with doubled capacity | amortized O(1) per token |
| `subseq(pos, n)` | a view of the same storage | O(1) |
| `size`, `empty`, `[]`, `data`, `begin`, `end` | read natively; elements are in a lazily materialized array | O(1) after materialization |

**Why value semantics survive appending in place.** A token array is a view
of a prefix of a buffer that is only ever appended to. An append writes in
place only when the sequence ends exactly at the buffer's used size, so every
other view of the buffer still sees exactly what it saw. Two copies at the
same end: the first appends in place, and the second (no longer at the end)
copies.

**Interface.** It is a contiguous, borrowed range (and a view) of `const
token`:
- `size`, `empty`, `data`, `begin`, `end`, `[]`, `front` and `back`;
- `subseq(pos, n = npos)`;
- `+=` and `+` with tokens and sequences, and `==` between them;
- construction from an initializer list of tokens, from any contiguous range
  of tokens (natively), or from any other range of tokens (one at a time).

There is no `token_view` (sequences are as cheap to copy as views were), and
no mutation of elements.

**Stress workloads**, doubled sizes, best of 5 (`stress4.sh`; seconds above
baseline):

| workload | original | vector | new | new / original |
|---|---|---|---|---|
| small × 2000 | 0.24 | 0.49 | 0.27 | 1.13× |
| enum 200 × 10 | 0.41 | 0.53 | 0.43 | 1.06× |
| enum 20 × 100 | 0.23 | 0.31 | 0.21 | 0.93× |
| enum 2 × 1000 | 0.31 | 0.29 | 0.20 | 0.66× |
| split 400 × 10 | 0.23 | 0.88 | 0.18 | 0.79× |
| split 40 × 100 | 0.24 | 0.84 | 0.18 | 0.75× |
| split, hoisted, 400 × 10 | 0.25 | 0.56 | 0.16 | 0.65× |
| split with `subseq`, 400 × 10 | 0.21 | 0.42 | 0.16 | 0.75× |
| split with `subseq`, 40 × 100 | 0.23 | 0.35 | 0.14 | 0.62× |
| scan 400 × 30 | 0.33 | 0.32 | 0.25 | 0.78× |
| scan 40 × 300 | 0.27 | 0.19 | 0.15 | 0.57× |
| pass by `const&`, 200 × 20 | 0.07 | 0.36 | 0.09 | 1.3× |
| pass by value, 200 × 20 | 0.06 | 0.52 | 0.11 | 2.0× |

For the original and vector models, the split-with-`subseq` rows are their
own block-split versions (`subrange` and `append_range` in the vector model).

The native accessors matter. Before `size`, `[]`, `subseq` and the rest were
native, the `subseq` split ran at 0.98–1.28×: each `names[i]` was an
interpreted call through `data()`.

**Memory** (peak RSS): `enum` 2 × 1000 is 289 MB in the original model and
153 MB here, with a baseline of 147–149 MB. `split` 40 × 100 is 174 MB
against 155 MB.

**Tests and examples** (90 cases): 121.13 s (original), 121.25 s (vector),
118.78 s (new), which is 1.9% faster than the original. Part of that is the
native member queries, which only the new compiler has.

### No nameable token-array type

The first version of this stage still had two layers visible in `<meta>`:
- the compiler's token array, named there as `__token_array =
  decltype(^^{ })` (only valid before the class was defined);
- a class holding one, plus its size.

Now `<meta>` names neither:
- **Storage:** the class declares no data members. When
  `std::meta::token_sequence` is defined, the compiler adds its storage: a
  private member no program can name, which reflection skips.
- **Literals:** a token literal is created with the class type.
- **Conversions:** the compiler converts between the class and its internal
  array only where it needs to (raw arguments, metafunction arguments and
  results, interpolation, injection, a macro's result). These are two implicit
  conversions only Sema forms, which template instantiation keeps rather than
  recomputes.
- **`<meta>` is required:** without it there is nothing for a literal to be,
  so `^^{ ... }` is an error, as `typeid` is without `<typeinfo>`. The clang
  tests that use token sequences without the library include a minimal
  definition (`clang/test/Reflection/Inputs/token-sequence.h`) whose operators
  are declared but not defined, since the compiler does them natively.

It is also faster: a sequence given to the compiler is a field read, not a
member-call intercept, and the object is a single field.

| workload | new / original, before | after |
|---|---|---|
| small × 2000 | 1.13× | 1.13× |
| enum (3 shapes) | 0.66–1.06× | 0.72–0.94× |
| split, token at a time | 0.75–0.79× | 0.57–0.59× |
| split, hoisted | 0.65× | 0.42× |
| split with `subseq` | 0.62–0.75× | 0.36–0.47× |
| scan | 0.57–0.78× | 0.55–0.56× |
| pass by `const&` / by value | 1.3× / 2.0× | 0.37× / 0.80× |

Tests and examples (90 cases): 2.0% faster than the original model.

## Conclusions

1. **Compile time is not a reason to reject `vector<token>`.** With a dedicated
   specialization and native fill and join, real code shows no measurable
   difference, and token-heavy stress code is roughly 1–2×.
2. **A stock `std::vector` is the wrong measurement.** Constant-evaluating it
   costs hundreds of microseconds per allocation and tens per element, in any
   clang. Any vector model needs a specialization like this one.
3. **If the vector model is adopted, there are two things to optimize:**
   - make `push_back` (or the whole append path) native;
   - avoid allocating for literals without interpolations, for example by
     caching or by a copy-on-write representation for them.
4. **The current model has real costs of its own:**
   - quadratic memory for `+=` in a loop, fixable with an arena append or a
     builder;
   - about 25 µs per token for iteration, fixable by exposing a sequence's tokens
     as a contiguous array of scalar tokens.

   Both fixes are also what a "`token_sequence` is a range of scalar `token`"
   design needs, which would keep the handle's cheap copies while getting most
   of the ergonomics.
5. **Separately:** expanding macros at parse time instead of per instantiation
   is worth about 10× per use when it applies (`fwd!`: 2.5 µs against 28 µs),
   and has semantic benefits as well.

## Reproducing

| what | where |
|---|---|
| `fwd!` expansion benchmark | `~/tmp/fwdbench/` (`gen.py`, `run.sh`) |
| simulation workloads (stages 1–4) | `~/tmp/tokbench/gen.py`; models `ts`, `vec` (with round trips) and `vec2` (without); vector selected with `-DVEC_LEAN`, `-DVEC_SCALAR`, `-DVEC_BULK` or `-DVEC_NATIVE`; runners `runall.sh`, `runall2.sh`, `runbest.sh`, `runnative.sh` |
| vector implementations | `~/tmp/leanvec/` (`lean_vector.h`, `scalar_vector.h`, `bulk_vector.h`, `native_vector.h`) |
| microbenchmarks | `~/tmp/tokbench/lit_*.cpp`, `vint_*.cpp`, `pv_*.cpp`, `iter_*.cpp`, `nv_*.cpp`, `e{A..E}.cpp` |
| full implementation, tests and examples | `~/tmp/tvbench/run.py` (results in `results.json`); migrated example copies in `~/tmp/tvbench/examples-new/` |
| full implementation, stress workloads | `~/tmp/tvbench/gen2.py` (models `ts`, `tv` and `vw`), `stress.sh`, `stress-split.sh`, `stress-block.sh` (block-at-a-time splitting), `stress3.sh` (all three models), `stress3-big.sh` (doubled sizes, best of 5) |
| view model, tests and examples | `~/tmp/vwbench/run3.py` (results in `results.json`); migrated example copies in `~/tmp/vwbench/examples-new/`; literal microbenchmarks in `~/tmp/vwbench/micro/` |
| the vector-model compiler | saved in `~/tmp/clang-tv-model/` with its sources; flags in `~/tmp/flags-tv-model` |
| compiler-owned `token_sequence` (stage 7) | stress: `~/tmp/tvbench/stress4.sh` (model `ns` in `gen2.py`); tests and examples: `~/tmp/nsbench/run4.py`, migrated example copies in `~/tmp/nsbench/examples-new/` |
| the previous compiler | saved in `~/tmp/clang-ts-model/` (compiler, headers, libraries, and the original test and example sources), with flags in `~/tmp/flags-ts-model` |

Stages 1–4 depend on the experimental `fill` and `join` metafunctions
(`std::meta::__experimental` in the current-model `<meta>`). The saved compiler
includes them.
