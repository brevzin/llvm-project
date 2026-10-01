# P3549 breakage survey (2026-09-30 – 2026-10-02)

## Bottom line
- **Scope:** 11 codebases, 15,617 TUs: 7 open-source projects and 4
  proprietary codebases. About 15,000 TUs were checked fully; the rest
  fail for reasons unrelated to P3549.
- **New errors:** 38 TUs, all from **5 root causes**. Every one is a library
  that pattern-matches a callable's return type (`void`, or "all arms the
  same") instead of testing convertibility.
  - 3 of the 5 are `visit`-style dispatch: `std::visit`,
    `boost::variant2::visit`, `jmp::visit`.
  - The other 2 special-case `void`: LLVM's `ErrorHandlerTraits` and
    libunifex's `then` / `just_from`.
  - None involves `noreturn_t` being a wrong type for the expression. Each
    needs a library fix in one place.
- **Compat sites:** 244 places where a type changed (`-Wdiverging-compat`).
  - 231 are deduced return types (almost all lambdas), and 13 are comma
    expressions from one assertion macro.
  - All but the 38 TUs above still compile.

| | TUs | checked | compat sites | new-error TUs | root causes |
|---|---:|---:|---:|---:|---:|
| open source (7 projects) | 7,664 | 7,524 | 60 | 36 | 4 |
| proprietary (4 codebases) | 7,953 | 7,475 | 184 | 2 | 1 |
| **total** | **15,617** | **14,999** | **244** | **38** | **5** |

## Method
Each project's compilation database is re-run with the prototype clang in
`-fsyntax-only` mode, with `-fdiverging-expressions -Wdiverging-compat`. TUs
that fail are re-run with the feature off, to separate new errors from
pre-existing ones.
- **Open source:** the project's own -std (C++17/20), libstdc++ 13, plus a
  libc++ pass for fmt, range-v3, flux and libunifex.
- **Proprietary:** each codebase's own build configuration (C++26,
  libstdc++ 13/14).

`-Wdiverging-compat` is an opt-in warning added for this survey. It flags
each place a type differs from before:
- a function or lambda deduced as noreturn_t (was void),
- `(throw x, y)`,
- `c ? throw a : throw b`,
- `decltype(throw x)`.

## Results

| project        |  TUs | pre-existing failures | compat sites | new-error TUs |
|----------------|-----:|---------:|------:|------------:|
| LLVM + clang + clang-tools-extra | 5387 | 28 | 38 | 16 |
| Boost (19 libs + tests) | 1025 | 30 | 7 | 2 |
| libunifex      |  119 |  1 |  9 | 18 |
| spdlog         |  140 |  0 |  3 |  0 |
| toml++         |    6 |  0 |  1 |  0 |
| flux           |  131 | 80 |  2 |  0 |
| range-v3, mp11, abseil, Catch2, quill, fmt, vir-simd | 856 | 1 | 0 | 0 |
| (internal) | 7953 | 478 | 184 | 2 |

All 60 open-source compat sites are deduced return types. In the internal code-bases, 13 compat sites are commas, 3 are functions, and the rest are lambdas.

## Root causes of every new error (5)
1. **LLVM `handleAllErrors` / `ErrorHandlerTraits` (16 TUs, 11 handler
   lambdas).** The traits are specialized for handlers returning `Error` or
   `void`. A handler that ends in `report_fatal_error` / `llvm_unreachable` /
   `exit` now returns `noreturn_t` and matches neither. Fix: map `noreturn_t`
   like `void` (two specializations).
2. **`std::visit` (libunifex `stop_when.hpp`, 15 TUs).** The visitor ends one
   `if constexpr` branch in `std::terminate()`. Instantiations now return
   `void` or `noreturn_t`, and visit requires a single return type. Same
   with libc++.
3. **`boost::variant2::visit` (histogram, 2 TUs).** `static_if` picks between
   a lambda returning void and one that throws, so the result type differs
   across alternatives.
4. **`jmp::Variant::inspect` / `jmp::visit` (internal, 2 TUs).**
   - The visit static-asserts that every arm returns the same type. Its
     `mp_with_index` also takes the result type from case 0, so returning
     `void` from a `noreturn_t` function is ill-formed.
   - The visitors have "impossible state" arms that only throw, directly or
     via a `[[noreturn]]` helper:
     ```cpp
     state.inspect(
         [](StateLogin const&)     { throw_error(Error::AlreadyLoggedIn); },  // now noreturn_t
         [](StateConnected const&) { throw_error(Error::AlreadyLoggedIn); },  // now noreturn_t
         [&](StateDisconnected const&) { /* ... */ });                        // void
     ```
5. **libunifex `then` / `just_from` (3 TUs).**
   - `then(sender, f)` derives the values it sends from `f`'s result type,
     with a `void` special case: a `void` result sends no values
     (`set_value()`), and any other result `R` sends one `R`. `just_from(f)`
     is `then(just(), f)`.
   - A function that only throws now returns `noreturn_t`, so the sender
     advertises a `set_value(noreturn_t)` completion instead of
     `set_value()`:
     ```cpp
     auto s = just_from([]() { throw 1; });
     // value types were type_list<type_list<>>, now type_list<type_list<noreturn_t>>
     ```
   - That breaks code expecting a sender of no values. The type-erased
     `any_sender_of<>` can't accept it (2 TUs), and `async_scope::spawn` no
     longer gives `future<>` (1 TU, a test's `static_assert`).

Causes 2–4 are the same idiom: a visitor with throwing arms. They have the
same fix: when computing a visit's result type, ignore `noreturn_t` arms
(absorb them, as `?:` does).

Causes 1 and 5 are the same idiom: a `void` special case for a callable's
result. They have the same fix: treat `noreturn_t` like `void`. For `then`,
the precise fix is to advertise no value completion at all, since `f` never
returns.

## Compat sites that still compile
- Handlers stored in `std::function<void(...)>` or passed to templates
  that only call them (OptTable::parseArgs, spdlog's set_error_handler).
- Captureless lambdas converted to `void(*)(...)` (libunifex `_ref`).
- `std::thread` / `std::async` / `std::future<noreturn_t>`. These rely on
  `t.~T()` being valid for `noreturn_t` (libstdc++'s `is_destructible` uses
  it), so `noreturn_t` must support pseudo-destructor calls.
- toml++'s `TOML_RETURNS_BY_THROWING auto finish()` now returns noreturn_t,
  which matches the annotation's intent.
- **The only non-deduction sites (internal, 13):** all come from one
  assertion macro:
  ```cpp
  #define ALWAYS_ASSERT(expr) \
      ((expr) ? static_cast<void>(0) : (trigger_assertion(#expr), __builtin_unreachable()))
  ```
  The inner comma is now noreturn_t, but the enclosing `?:` absorbs it, so
  the assertion is still `void`. Harmless.
- No `c ? throw a : throw b` or `decltype(throw x)` sites anywhere.
