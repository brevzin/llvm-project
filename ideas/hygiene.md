# Hygiene for Token Sequence Macros (P4380)

Notes on name hygiene for token-sequence injection and macros, drawing on
Justin Pombrio's dissertation *Resugaring: Lifting Languages through Syntactic
Sugar* (Brown, 2018) and the Racket/Scheme hygiene literature, applied to
[P4380R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4380r0.html)
(Token Sequence Injection).

Contents:

1. [Background: what Pombrio contributes](#1-background-what-pombrio-contributes)
2. [Hygiene: two separate choices](#2-hygiene-two-separate-choices)
3. [Capture and anaphora](#3-capture-and-anaphora)
4. [Trees vs. tokens: what injecting trees does and doesn't give you](#4-trees-vs-tokens)
5. [A test suite for any design](#5-a-test-suite-for-any-design)
6. [Where P4380R0 stands today](#6-where-p4380r0-stands-today)
7. [Proposed shape: identifiers carry the scope where they were written](#7-proposed-shape)
8. [Walking through the paper's examples](#8-walking-through-the-papers-examples)
9. [What the paper would need to specify](#9-what-the-paper-would-need-to-specify)
10. [References](#10-references)

---

## 1. Background: what Pombrio contributes

**Thesis in one sentence:** when a language feature is defined by rewriting
into other code (a macro, or built-in sugar like range-`for`), tools end up
describing the rewritten code rather than what the user wrote. This affects
debugger stepping, name binding and type errors. Pombrio shows how to
automatically work out that information at the level the user wrote
("resugaring"), with correctness proofs.

Terminology:

- **Surface language**: what the programmer writes, including macros.
- **Core language**: what's left after all macros are expanded.
- **Desugaring**: macro expansion.
- **Resugaring**: mapping information from the core language back to the
  surface language.

Notation primer:

| Notation | Meaning |
|---|---|
| `p ⇒ p'` | Macro rule: pattern `p` expands to template `p'` |
| `α, β, δ` | Pattern variables (macro parameters) |
| `e/p` | Match term `e` against pattern `p`, producing bindings |
| `γ • p` | Substitute bindings `γ` into pattern `p` |
| `xᵈ`, `xʳ` | Variable declaration vs. reference |
| `Γ ⊢ e : t` | In environment Γ, `e` has type `t` |
| fraction bar | premises above ⇒ conclusion below |
| `a ≤ b` (ch. 5) | there is a scope path from a to b ("b is visible at a") |

### Chapter summaries

- **Ch. 1, when to use macros (§1.3).** Use sugar only when the abstraction
  can't be written with functions or classes. In most languages that means:
  (1) binding constructs, (2) delayed or conditional evaluation, (3) defining
  data, and in the examples (4) control flow that escapes the enclosing
  function. This is a ready-made motivation section for C++ macros (`assert`,
  logging, `try_`, anaphoric lambdas, `vec!`). Reflection now covers much of
  (3).

- **Ch. 2, a taxonomy of macro systems.** Axes:
  - representation: text, tokens, concrete syntax, abstract syntax (AST)
  - authorship
  - metalanguage: pattern rules vs. the language itself
  - expansion order: OI (outside-in, the macro sees unexpanded arguments) vs.
    IO (inside-out, arguments expanded first)
  - parameter kinds, result kinds
  - deconstruction: can the macro take its arguments apart?
  - macros that define macros
  - four levels of safety: syntactic, hygiene, scope, type

  His position on representation is blunt: *"code transformations should
  never operate at the level of text (or token streams)."* His definition:
  **"hygiene is lexical scoping for macros."** The C++ entries in his table
  cover only the C preprocessor and templates (g++ 4.8). His conclusion
  that C++ has no real macro system is the gap P4380 addresses.

- **Ch. 3, restrictions.** All results assume declarative, pattern-based
  rules. Parameters must not be duplicated in the expansion (the C
  `MAX(a,b)` double-evaluation bug turned into a formal rule), and rule
  left-hand sides must not overlap.

- **Ch. 4, resugaring evaluation (debugger stepping).** Tag every generated
  AST node with where it came from: a rule, or user source. Try to reverse
  the rule after each step, and skip steps that can't be shown faithfully.
  Two properties are proven: Emulation (every shown step expands to the real
  step) and Abstraction (macro internals are never shown, user code is never
  hidden). A `!` marker lets the macro author make parts of the expansion
  visible. Relevance to C++: origin tracking should be part of the design,
  because debug info and diagnostics depend on it.

- **Ch. 5, resugaring scope (IDE support).** Given the core language's
  scoping rules, infer each macro's scoping rules from its definition alone,
  once per macro. It also gives a stronger, testable definition of hygiene:
  **expansion preserves α-equivalence**, i.e. consistently renaming a user's
  variables never changes the program's meaning after expansion. It splits
  scope into *imports* (what a construct makes visible to its parts) and
  *exports* (what it passes back out to its parent).

- **Ch. 6, resugaring types.** Type-check the macro's expansion template at
  definition time, treating each parameter as an unknown type. Solve for the
  unknowns and drop the internal steps; what remains is a type rule for the
  macro (roughly, an automatically inferred concept). A use type-checks
  against that rule iff its expansion type-checks, so errors are reported at
  the macro use and bad macros are rejected at definition. Supporting
  features: `calc-type` (≈ `decltype`), a **capture list** (hygienic by
  default, explicit exceptions), and globals resolved at the definition
  (≈ non-dependent names in two-phase lookup). He notes that analysing
  macros mixed in with ordinary code is "a much harder problem" and leaves
  it to future work.

### Main trade-off

Every guarantee in the thesis depends on macros being declarative pattern
rules. Procedural macros (arbitrary code producing code, which is what P4380
has) lose them. That argues for either a Rust-style two-tier design
(declarative plus procedural) or for recovering as much as possible in the
procedural model. The rest of this document is about the second option for
name hygiene.

---

## 2. Hygiene: two separate choices

Every identifier in an expansion was written either by the **user** (it came
in through an argument) or by the **macro author** (it's in the macro body).
Hygiene means each kind is looked up where it was written.

The macro's own text contains both references and declarations, and each
needs its own choice:

| Macro-written... | Default (hygienic) | Explicit opt-out |
|---|---|---|
| **Reference** (`report_failure`, `std::abs`) | Looked up at macro **definition** | Looked up at the **injection** site |
| **Declaration** (`lhs`, `rhs`, `_1`) | **Invisible** to user code (fresh) | **Visible** to user arguments (capture, i.e. anaphora) |

It prevents two kinds of accident, which `assert_eq` can hit both of:

**(a) A name the macro introduces hides the user's name.**
```cpp
// expansion of assert_eq(x, y):
{
    auto&& lhs = x;
    auto&& rhs = y;
    if (!(lhs == rhs)) report_failure(lhs, rhs, "x", "y");
}
```
If the user writes `assert_eq(lhs, rhs + 1)`, a naive expansion gives
`auto&& lhs = lhs;` and `rhs + 1` refers to the macro's temporary.

**(b) The user's name hides a name the macro refers to.** If the caller has a
local `report_failure`, a naive expansion calls it instead of the macro
author's function.

Hygiene prevents both. For `assert_eq` you want no exceptions.

---

## 3. Capture and anaphora

"Capture", in Pombrio's sense and in the Racket literature, is **not** about
names the caller passes in. It means a **name the macro introduces that
user code is deliberately allowed to see.**

### Pombrio's mechanism (§6.4.3)

Every desugaring rule carries a set of *capturing* variables. Any identifier
introduced by the right-hand side that is not in that set gets a fresh name
during expansion. In his tool SweetT this is a `#:capture` clause. All the
demos in Figures 20–23 have `#:capture()` empty:

```racket
(ds-rule "or" #:capture()
  (or ~a ~b)
  (let x = ~a in (if x x ~b)))
```

The thesis never prints the `foreach` source. It only says in prose that the
sugar "is declared to capture the variable `break`" (§6.6.2). The shown
capturing example is `λret` (Figure 18):

```
λret x:T. b  ⇒  λ x:T. try (let return = (λ v:Str. raise v) in b) with (λ v:Str. v)
```

with `return` "automatically bound (i.e., marked as capturing)".

### The `foreach` example explained

```
foreach x list body
⇒
letrec loop = λ lst acc.
    if isnil lst then acc
    else try
           let break = λ _. raise "" in     // break() throws
           let x = head lst in              // bind the USER's chosen name
           loop (tail lst) (cons body acc)  // evaluate body, accumulate, recurse
         with λ _. acc                      // caught break: stop, return results so far
in reverse (loop list nil)
```

It's a **map with early exit**: `body` is evaluated once per element with
`x` bound, and the results are collected. `break` is a function that throws.
Each iteration's `try` catches it and returns the results so far. `acc` is
built backwards, hence the `reverse`.

Roughly the same thing in C++:
```cpp
// foreach(x, list, body)
[&] {
    std::vector<F> __acc;                                   // fresh, invisible to body
    struct __break_t {};                                    // fresh
    auto break_ = []() -> auto { throw __break_t{}; };      // CAPTURING
    try {
        for (auto&& x : list)                               // x is the user's name
            __acc.push_back(body);
    } catch (__break_t) {}
    return __acc;
}()
```

Where the names come from:

| Name | Origin | Visible in `body`? |
|---|---|---|
| `x` | User (a macro argument, so the user chose the spelling) | Yes, like a lambda parameter |
| `loop`, `lst`, `acc`, `_`, `v` | Macro, not captured | No, renamed fresh |
| `break` | Macro, **captured** | Yes |

Derived type rule (Figure 17):
```
Γ ⊢ list : List D      Γ, x : D, break : (Unit → B) ⊢ body : F
───────────────────────────────────────────────────────────────
              Γ ⊢ foreach x list body : List F
```
- The environment for `body` has exactly `x` and `break` added. His `t-fresh`
  rule removes `loop`, `lst` and `acc`.
- `break : Unit → B` with `B` free, because `break` never returns (it
  behaves like `[[noreturn]]`).
- No `letrec`, `try` or `reverse` in the rule. The implementation is hidden.

**Leak in his example:** since `break` is `raise ""` caught by
`with λ_. acc`, *any* string exception thrown by the user's `body` is
silently treated as `break`. Hygiene covers names but not control-flow
effects. C++ macros that wrap user code in `try`, loops (bare `break` or
`continue` in an argument) or lambdas (bare `return` or `co_await` in an
argument) need a position on this "control-flow hygiene".

### Anaphoric `λ!(_1.y < _2.y)`

The expansion `[&](auto&& _1, auto&& _2) -> decltype(auto) { return <body>; }`
introduces declarations `_1`, `_2` that the user's argument is meant to see.
That is capture on purpose, declared per name. Everything else the macro
introduces stays fresh. Nesting behaves like ordinary lexical scoping
(`λ!(any_of(v, λ!(_1 < _2.y)))`: the inner `_1` refers to the inner lambda).

### Refinement: Racket syntax parameters

With plain capture, a bare `_1` outside any `λ!` is unbound or finds
something unrelated. Racket's syntax parameters (Barzilay, Culpepper &
Flatt 2011) declare `_1` once, globally, with a default meaning of "error:
used outside `λ`", and `λ` rebinds it for the extent of its body. You get a
clear diagnostic outside the macro and a real, documentable entity. Racket
does anaphoric `it`/`return` this way.

### A fourth case not covered by the thesis

A macro that deliberately refers to a name in the *caller's* scope that
wasn't passed in ("use whatever `logger` is in scope where I'm expanded").
This is the reverse of capture. Scheme and Racket allow it through explicit
escapes (`datum->syntax`), and it's usually considered a code smell. If
supported, it should be an explicit per-name opt-in.

---

## 4. Trees vs. tokens

Injecting trees instead of tokens gives **syntactic** safety: no
`SUB(a,b)` precedence bugs and no unbalanced output. It doesn't give **name**
hygiene by itself. What matters is what an identifier in the tree carries.

- **Plain syntax trees (parsed, names not resolved):** no hygiene. The
  user's `lhs` is an identifier node spelled "lhs". Looked up after
  injection, it finds the macro's `lhs`.
- **Fully resolved trees (analyzed at the call site):** full hygiene for user
  arguments, but **anaphora are impossible**. `_1.x > _2.x` can't be analyzed
  at the call site because `_1` doesn't exist there, and `.x` depends on a
  type only known later. C++ makes this worse: dependent expressions, ADL,
  overload resolution against the macro's types, `auto` parameters.
- **Identifiers that remember where they were written (what works):**
  lookup happens after injection, but each identifier carries its lexical
  context. User-written identifiers carry the call site's scopes;
  macro-written identifiers carry the macro definition's scopes. Capture
  means the macro places a declaration into the user's context. This is
  Racket's "sets of scopes" model (Flatt, POPL 2016, which Pombrio relates
  his preorder to in §5.3.4).

This resembles two-phase lookup: non-dependent names bind at the template
definition, and dependent names resolve at instantiation in a defined context.

---

## 5. A test suite for any design

1. **Macro temporary vs. user name:** `assert_eq(lhs, rhs + 1)`. The user's
   names must mean the user's variables.
2. **User local vs. macro reference:** the caller has a local
   `report_failure`, and the macro must still call its own.
3. **Anaphor:** `λ!(_1.x > _2.x)` works, with `_1.x` resolved after the
   parameter types are known.
4. **Nested anaphor:** `λ!(any_of(v, λ!(_1 < _2.y)))`. The inner `_1`
   means the inner lambda's parameter.
5. **Anaphor outside its macro:** a bare `_1` elsewhere gives a clear error,
   not something odd.
6. **User-named binder:** `foreach(x, xs, f(x))`. The user chose `x`, and
   the macro binds it for the body. A stronger version is
   `fun!((x, y) => x > y)`, where the user writes every identifier and the
   macro only rearranges them into `[](auto&& x, auto&& y) { return x > y; }`.
7. **Control flow in arguments:** `break`/`return`/`co_await` inside an
   argument the macro wraps in a loop, lambda or `try`.

| Model | Fails |
|---|---|
| Tokens | 1, 2, 7 |
| Plain trees | 1, 2 (syntax is fixed, names aren't) |
| Fully resolved trees | 3, 4 |
| Identifiers carry context + explicit capture | none of 1–6; 7 needs its own rule |

---

## 6. Where P4380R0 stands today

### What the paper does

- `^^{ ... }` is a token-sequence literal. It is lexed, not parsed, until
  injection.
- `\(e)` interpolates: a token sequence is spliced in; an `info` becomes a
  single artificial token meaning that entity; otherwise a token meaning the
  value.
- `std::meta::id(...)` creates an identifier token; `str_lit(...)` creates a
  string literal.
- Macros (`__macro`) return token sequences. **Typed parameters** are
  reflections of the argument expression, and interpolating them inserts the
  *expression* ("expression identity is preserved"). **`token_sequence`
  parameters** receive raw call-site tokens.
- §3.2 notes that typed arguments make macros "partially hygienic": local
  `lhs`/`rhs`/`expr` in `check!` never conflict with names in the argument.
- §3.4 calls `λ!` "necessarily unhygienic".

So the user-argument side of hygiene is already handled for typed parameters,
by the "fully resolved trees" approach, and anaphora are handled through
`token_sequence` parameters plus `id()`.

### The problem

Identifiers written in a `^^{...}` literal are looked up **at the injection
site**. So macro-written references are *not* hygienic by default. For

```cpp
return ^^{ ... if (not (lhs == rhs)) report_failure(lhs, rhs, ...); };
```

`report_failure` is looked up where the macro is expanded. The escape today
would be `\(^^report_failure)`.

### Evidence from the paper itself

**§3 macros are already made hygienic by hand.** Nearly every macro body
fully qualifies names from the global namespace down:
- `::check_fail` (§3.2)
- `::std::vector<\(^^T)>` (§3.9)
- `::my::impl::adl_begin` (§3.8)
- `::LogLevel::`, `::std::basic_format_string` (§3.10)

That is manual definition-site lookup, and a few spots were missed:
- **`check!` calls `fwd!(lhs)` unqualified.** If `fwd` lives in a library
  namespace, `check!` stops compiling for users who can't see `fwd`
  unqualified, or picks up their own `fwd`. The same applies to `fwd!` in
  `try_!` and `define_op!`.
- **`my_tuple::operator[]` writes `std::get`** without `::`, so it breaks
  inside any user namespace that has a nested `std`.
- **§3.1 warns against `static_cast<T&&>(\(t))`** because `T` in the output
  doesn't mean the macro's `T`. That warning exists only because literal
  identifiers are looked up at the injection site.

**`\(^^name)` doesn't cover the common cases:**
- **Overload sets can't be reflected.** `check_fail` has two overloads, so
  `\(^^check_fail)` is ill-formed. Library helpers are often overloaded.
- **Access.** A macro using a private or `detail` helper produces code at the
  injection site that can't access it. Qualification doesn't help.
- **Forgetting it fails silently.** An unmarked name usually still resolves
  to *something*, so the macro passes the author's tests and breaks, or
  changes meaning, in user code. Opt-in hygiene mostly won't happen.

**§2 depends on the opposite behavior.** `inject_erasing_ctor` (§2.1) is a
namespace-scope function whose literal refers to `DynRef`, `data`, `vtable`,
`vtable_for` and `satisfies_interface`. They exist only in the class being
injected into. `inject_vtable_for` refers to `VTable`, which a *different*
injection created. If literal tokens simply used definition-site lookup, the
type-erasure example breaks.

So one `token_sequence` type serves two jobs with opposite natural defaults:
- **Macros (§3)** behave like opaque function calls and want definition-site
  lookup.
- **Injection into a class (§2)** behaves like writing members into that
  class and wants to find the class's members.

---

## 7. Proposed shape

Identifiers remember where they were written. There is one lookup rule, plus
the grammar rules C++ already has for what a declaration declares:

1. **Literal scope.** An identifier written inside `^^{...}` is looked up,
   at injection time, from the scope where the literal appears. Lookup is
   at injection because the tokens aren't parsed until then, but it happens
   *from the recorded scope*. This is an annotation token carrying (identifier,
   scope), much like the artificial tokens `\(info)` already produces and
   roughly what an instantiated dependent name already carries. Because
   lookup is in a real scope, it handles overload sets and templates (so the
   parser knows whether `<` starts template arguments). Access checking is a
   separate question with its own rule (rule 6).

2. **Names being declared are resolved in the target.** When declarations
   are injected into a class or namespace (`queue_injection`,
   `inject_members`, `queue_injection(^^std, …)`), the names that the grammar
   says are being declared or defined there belong to the target:
   - declarator-ids (the name of the injected function, variable, or type)
   - the class-head name of a specialization (`formatter` in
     `template<> struct formatter<X>` injected into `std`)
   - a constructor's name (`DynRef(...)`)
   - mem-initializer-ids (`data(...)`, which is already looked up in the
     constructor's class)

   None of these were ever ordinary unqualified lookup, so this isn't a
   second lookup rule. **Uses** of target names in expressions and types
   follow rule 1 like everything else. To refer to the target, name it
   explicitly:
   - `this->x` for members inside member function bodies (type-based
     member lookup, unaffected by where the token was written)
   - `\(cls)::x` for static members, nested types and member templates,
     where `cls` is a reflection of the target (`iterator_interface` already
     takes one)
   - `id("x")` for anything else at the injection site

   This is the rule C++ already applies to **dependent base classes**. When a
   template is written, the base's members aren't known, so unqualified
   lookup doesn't find them, and you write `this->x` or `Base::x`. The
   injection target is in the same position: unknown when the literal is
   written. It is also Racket's answer: code a macro injects into a class
   doesn't see that class's members unless the macro borrows the class's
   context explicitly (`datum->syntax`).

   Keeping a single lookup chain is the point. "Search the target first,
   then the literal's scope" looks attractive, but it searches two unrelated
   scope chains, so target names hide the macro author's names, which is
   the non-hygiene rule 1 removes. It also needs a special case to keep
   expression macros hygienic. Out-of-line definitions (`void X::f() { x; }`)
   aren't a precedent for it, because they must appear in a scope enclosing
   `X`, so their lookup is still one nested chain.

3. **One injection is one unit.** All tokens in one `queue_injection` call or
   one macro result belong to the same expansion, regardless of how many
   literals they were assembled from.

   What matters is where a declaration's **name token** came from, not who
   assembled the surrounding code:
   - A block-scope declaration whose name is a **macro-written** token
     (`lhs`, `rhs`, `res`, `__r`, `obj`, `T`) is private to the expansion.
     All macro-written tokens of that expansion can see it; user-written
     tokens can't.
   - A declaration whose name is a **user-written** token (from a
     `token_sequence` argument) or was made by `id()` behaves as if the user
     wrote it, so user-written tokens can see it. Examples: `fun!`'s
     parameters (§8.1), `foreach`'s `x`, `define_op!`'s `\(name)`, `λ!`'s
     `id("_", i)`.
   - Class-scope and namespace-scope declarations stay visible whatever
     their name's origin. They are the output (`VTable`, `define_op!`
     structs, the logging example's injected macros). Racket hides these
     too by default, and it's widely regarded as annoying there.

   This is how Racket decides binding: whether a binder can see a reference
   depends on the binder identifier's own context.

4. **`id(...)` is the explicit opt-out.** An identifier with no recorded
   scope is looked up at the injection site. The paper already uses it
   exactly where unhygienic behavior is wanted (`λ!`'s `_1`…`_9`, names built
   from reflections). `λ!` becomes "explicitly captures `_1`…`_9` via `id()`".

5. **Arguments.**
   - Typed parameters: keep interpolating analyzed expressions (already
     right).
   - `token_sequence` parameters: tokens carry the call site's context, so
     the macro's local declarations can't capture them except through `id()`.

6. **Access is checked as the entity the token belongs to, not from where
   the literal was written.** C++ already separates lookup from access: find
   the name, then check access in the context of the entity whose definition
   contains it. `void X::f() { priv; }` defined at namespace scope has access
   because `f` is a member of `X`, not because of where the text sits.
   Applied to tokens:

   | Where the token ends up | Lookup (rule 1) | Access checked as |
   |---|---|---|
   | Body of an injected declaration (§2) | literal's scope | **the declared entity**. An injected member of `DynRef` has `DynRef`'s access, wherever the generator's literal was written. |
   | Macro expansion, macro-written token | literal's scope | **the macro being expanded**, i.e. its class membership and friendships |
   | Macro expansion, user-written token | call site | **the user** |

   Why not "access from the literal's context": in §2.1, `DynRef`'s `data`
   and `vtable` are **private** (they come before `public:`), and
   `inject_interface` is a namespace-scope function whose literal writes
   `this->data` and `this->vtable`. Checking access from the literal's
   location would stop `DynRef`'s own injected forwarders from touching
   `DynRef`'s private members.

   Why per token: the macro's privileges don't leak into its arguments, and
   the user's lack of privileges doesn't stop the macro. The two simpler
   alternatives are both wrong:
   - Check everything at the injection site (today's behavior): the macro
     can't use private helpers.
   - Check everything as the macro: any argument gets the macro's access.

   Macros should be able to be **friends**, like functions (placeholder
   syntax: `friend __macro check;`), so a namespace-scope macro can use a
   class's private helpers when the class grants it (§9, item 14).

   Rule 6 also makes access independent of the nested-literal question (§9,
   item 4). Whichever scope a nested literal's tokens record for lookup,
   their access comes from the macro being expanded.

### How this lines up with templates

| Templates | Proposed token rule |
|---|---|
| Non-dependent name bound at definition | Literal identifier resolved from the literal's scope |
| Dependent base members not found unqualified; write `this->x` or `Base::x` | Target members not found unqualified; write `this->x`, `\(cls)::x`, or `id("x")` |
| ADL still applies at instantiation | Unqualified calls from macro text get ADL at injection (qualify to suppress, as today) |
| Access checked in the template's context, not at the point of instantiation | Access checked as the macro being expanded, or as the injected entity (rule 6) |

Presenting this to EWG as "the template rules, applied to token literals" is
probably easier than presenting a new hygiene model.

---

## 8. Walking through the paper's examples

**Headline result:** §3 mostly gets simpler and nothing in it has to change.
§2 needs about ten edits in §2.1, §2.5 and §2.9, each turning a hidden
dependency on the injection target into an explicit one. The other §2
examples are unchanged.

### 8.1 §3 macros

#### `check!` (§3.2): qualification becomes relative to the author's namespace

With `check` and its helpers in a library namespace:

```diff
 namespace mytest {
   namespace detail { /* check_fail overloads */ }
   template <class T> __macro fwd(T&& t) { ... }

   template <class T>
   __macro check(T&& cond) {
       ...
       return ^^{
           do {
               auto&& lhs = \(ops[0]);
               auto&& rhs = \(ops[1]);
-              if (not (::mytest::fwd!(lhs) \(operator_of(cond)) ::mytest::fwd!(rhs))) {
-                  ::mytest::detail::check_fail(\(text), \(sloc), lhs, \(op), rhs);
+              if (not (fwd!(lhs) \(operator_of(cond)) fwd!(rhs))) {
+                  detail::check_fail(\(text), \(sloc), lhs, \(op), rhs);
               }
           }
       };
   }
 }
```

- `check_fail` is an overload set, so `\(^^check_fail)` could never have
  worked. The name now resolves where the author wrote it.
- The paper's unqualified `fwd!(lhs)` is a latent bug today that gets fixed
  with no textual change. The same fix applies in `try_!` and `define_op!`.
- `detail::` stays qualified on purpose. An unqualified call still gets ADL
  at the injection site, like templates, so a user's type with a
  `check_fail` in its namespace could take over. Qualifying still suppresses
  ADL.

**Forgetting an interpolation becomes diagnosable:**
```cpp
::check_fail(text, sloc, lhs, \(op), rhs);   // forgot \( ) around text and sloc
```
- **Today:** `text`/`sloc` are looked up at the call site. If the user has
  variables with those names, this silently compiles with the wrong values.
- **New rule:** they resolve to the macro body's locals, which don't exist
  when the expansion is compiled. That becomes an error: "`text` is a local
  variable of macro `check`; did you mean `\(text)`?" (see §9, item 2).

#### `vec!` (§3.9): compile-time entities from the macro can be named directly

```diff
     return ^^{
         do {
-            auto res = ::std::vector<\(^^T)>();
-            res.reserve(\(sizeof...(Args)));
+            auto res = std::vector<T>();
+            res.reserve(sizeof...(Args));
             \(emplace_back_loop);
             do_return res;
         }
     };
```
`T` is the macro's local alias and `Args` its template parameter pack. Both
are per-instantiation compile-time entities, as in a template body.

#### `ranges::begin` (§3.8): relative name, private helpers possible

```diff
-            return ^^{ ::my::impl::adl_begin(\(as_lvalue(r))) };
+            return ^^{ impl::adl_begin(\(as_lvalue(r))) };
```
The macro is `begin_fn::operator()`, a member of `begin_fn`, and rule 6
checks macro-written tokens as the macro being expanded. So `adl_begin` could
become a private static member of `begin_fn`. Today's rule can't support
that: the expansion is checked from the call site.

`check!` is different: it's a namespace-scope macro. If `check_fail` were a
private member of some class, the class would have to befriend the macro
(`friend __macro check;`, placeholder syntax; §9, item 14), or `check`
would have to be a static member macro of that class.

#### `my_tuple::operator[]` (§3.7): no textual change, latent bug fixed

`std::get<\(idx)>(\(self))` breaks today inside any user namespace with a
nested `std`. Under the new rule `std` resolves from where `my_tuple` is
defined.

#### `fwd!` typed (§3.1): same code, the warning changes

`static_cast<T&&>(\(t))` would now mean the macro's `T`, not whatever `T` is
at the call site. It's still wrong. `fwd!` is used on names (`fwd!(x)`), and
a name is always an lvalue, so `T` is always deduced as `U&`, `T&&`
collapses to `U&`, and the macro never moves. `type_of(t)` is `decltype` of
the argument as written, which is the *declared* type of `x`:

| `x` declared as | `T` | `static_cast<T&&>` | `type_of(t)` | `static_cast<type_of&&>` |
|---|---|---|---|---|
| `U& x` | `U&` | `U&` | `U&` | `U&` (same) |
| `U&& x` | `U&` | `U&` (wrong) | `U&&` | `U&&` |
| `U x` | `U&` | `U&` (wrong) | `U` | `U&&` |

`T&&` is right only for lvalue-reference variables, which is why the paper
says it's right "a decent amount of the time", and wrong in exactly the
cases that motivate forwarding. For non-name arguments (`fwd!(f())`,
`fwd!(std::move(x))`) the two agree. So `\(type_of(t))` stays. The
explanation becomes "`T` is deduced from the value category of the
expression, not the declared type of the name" instead of "`T` means
something unrelated at the call site".

#### `λ!` (§3.4): unchanged, and now explicitly anaphoric

It already declares parameters with `id("_", i)`, so under rule 4 the body's
`_1` still refers to them. Compare writing them in the literal:
```cpp
return ^^{ [&](auto&& _1, auto&& _2) -> decltype(auto) { return \(body); } };
```
Under rule 3 those `_1`/`_2` are private to the expansion, and the user's
`_1` gets "undeclared identifier". Capture only happens through `id()`.

#### `fun!((x, y) => x > y)`: works, because the user wrote every name

This isn't in the paper, but it's the natural contrast with `λ!`. The macro
takes a `token_sequence`, splits it at `=>`, and produces:

```cpp
fun!(e => e < 0)          // [](auto&& e) { return e < 0; }
fun!((x, y) => x > y)     // [](auto&& x, auto&& y) { return x > y; }
```

The implementation never calls `id()`; it only splices the user's own tokens.
This works today, and it works under the new rules:

| Token | Origin | Context |
|---|---|---|
| `x`, `y` in the parameter list | user | call site |
| `x`, `y` in the body | user | call site |
| `auto`, `return`, `[`, `]`, `{`, `}`, `>` | macro / user | keywords and punctuators aren't looked up |

The declarations and references have the same context, so they bind exactly
as if the user had written the lambda. That depends on rule 3 being phrased
in terms of the declaring identifier's origin. A literal reading of "block-
scope declarations in the output are invisible to user-written tokens" would
wrongly break this macro.

Things to watch:
- **Mixing in macro-written names still fails, deliberately.** If the
  implementation adds `auto&& it = \(p);` so users can write
  `fun!(e => it < 0)`, the user's `it` won't see it. Capture needs
  `id("it")`.
- **Tokens must keep their context through indexing and slicing** (§9,
  item 12). The macro pulls `(x, y)` and `x > y` out of the argument with
  indexing and subranges.
- **Rebuilt tokens are context-free** (§9, item 12). If the implementation
  rebuilds a name with `id(identifier_of(tok))` or round-trips through
  `stringize`/`tokenize`, the new token is looked up at the injection site.
  For an expression macro that's the call site, so it still works, but by a
  different mechanism.
- **Parsing compares tokens by spelling** (§9, item 13). Finding `=>`, `(`,
  `,` relies on `==`.
- **Not a hygiene issue:** with `[]`, `fun!(e => e < limit)` can't use a
  local `limit`. The macro probably wants `[&]`, or should let the user pick
  the capture.

#### `try_!` (§3.5) and `Eq` (§2.6): defensive `__` names become unnecessary

```diff
-        do [__r=\(e)] -> decltype(auto) {
-            if (not \(CT)::should_continue(__r)) [[unlikely]] {
-                return \(RT)::from_break(\(CT)::extract_break(fwd!(__r)));
+        do [r=\(e)] -> decltype(auto) {
+            if (not \(CT)::should_continue(r)) [[unlikely]] {
+                return \(RT)::from_break(\(CT)::extract_break(fwd!(r)));
```
The same goes for `__lhs`/`__rhs` in `Eq`. Reserved identifiers are being
used as a stand-in for hygiene. Typed arguments already make most of these
safe, but rule 3 turns "safe after some reasoning" into a guarantee.

#### `define_op!` (§3.6): unchanged

`\(name)` comes from a `token_sequence` argument (call-site context) and
declares a namespace-scope struct, which stays visible. The macro-written
`x`, `l`, `r`, `L`, `R`, `T` bind to declarations in the same expansion.
`fwd!` gets the latent-bug fix.

#### Logging (§3.10): works; raises the nested-literal question

The defensive `::LogLevel::` and `::std::basic_format_string` resolve the
same either way, since the literal and the injection are both inside
`Logger`. The inner `^^{…}` inside the injected macro body raises a new
question (§9, item 4). The output declares `auto& self = \(self);` while the
macro has a parameter named `self`, which raises §9, item 3.

**Making `do_log` private.** In the paper, `do_log` and `min_level` are
public only because the expansion lands at the call site (e.g. `main`) and is
access-checked there. Under rule 6 both can be private, and nothing else
changes:

```diff
 struct Logger {
+private:
     LogLevel min_level;

-    explicit Logger(LogLevel min_level) : min_level(min_level) { }
-
     auto do_log(LogLevel level, std::string_view fmt, auto&&... args) -> void {
         std::println("[{}] {}", level, std::vformat(fmt, std::make_format_args(args...)));
     }

+public:
+    explicit Logger(LogLevel min_level) : min_level(min_level) { }
     consteval {
         // unchanged: injects debug!, info!, error! as (public) member macros
     }
 };
```

- The `consteval` block stays in the public section, so the macros it
  injects are public.
- `self.do_log(...)` and `self.min_level` are macro-written tokens in the
  expansion of `debug!`, a member macro of `Logger`, so they are checked
  with `Logger`'s access.
- This doesn't depend on §9 item 4. Whether the inner literal's tokens
  record the `consteval` block or the injected macro body, access comes from
  the macro being expanded (`Logger::debug`).
- The user can't borrow that access:

  ```cpp
  log.info!("{}", (log.do_log(LogLevel::error, "sneaky"), 1));   // error: do_log is private
  ```

  The argument is a typed parameter, analyzed in `main` before the macro
  runs. A `token_sequence` argument would carry the call site's context and
  be checked as the user.

With the present model, would have to change this to be correct:

```diff
- if (\(level) >= self.min_level)      { self.do_log(\(level), \(call_args));      }
+ if (\(level) >= self.\(^^min_level)) { self.\(^^do_log)(\(level), \(call_args)); }
```

### 8.2 §2 injection: target names become explicit

The §2.1 functions are at namespace scope, so under rule 1 their literals
don't see `DynRef<I>`'s members. Names being declared (rule 2) still land in
the target. Uses of target members are written explicitly.

#### Type erasure (§2.1)

`cls` is passed in the way `iterator_interface` already does it (from the
class template, `^^DynRef` names the current specialization):

```diff
-consteval auto inject_erasing_ctor() -> void {
+consteval auto inject_erasing_ctor(std::meta::info cls) -> void {
     queue_injection(^^{
         template <class T>
-            requires (!std::same_as<std::remove_cvref_t<T>, DynRef>)
-                 and (satisfies_interface<std::remove_reference_t<T>>())
+            requires (!std::same_as<std::remove_cvref_t<T>, \(cls)>)
+                 and (\(cls)::satisfies_interface<std::remove_reference_t<T>>())
         DynRef(T&& t [[clang::lifetimebound]])        // constructor name: target (rule 2)
             : data((void*)(&t))                       // mem-initializer-id: target (rule 2)
-            , vtable(&vtable_for<std::remove_cvref_t<T>>)
+            , vtable(&\(cls)::vtable_for<std::remove_cvref_t<T>>)
         {}
     });
 }
```

```diff
 // inject_vtable_for: VTable was created by a different injection
-consteval auto inject_vtable_for(std::meta::info interface) -> void {
+consteval auto inject_vtable_for(std::meta::info interface, std::meta::info cls) -> void {
 ...
-        static inline constexpr VTable vtable_for = {
+        static inline constexpr \(cls)::VTable vtable_for = {
```

```diff
 // inject_interface
-        args += ^^{ data };
+        args += ^^{ this->data };
 ...
-                return vtable->\(vtable_func)(\(args));
+                return this->vtable->\(vtable_func)(\(args));
```
The `this->` version is what the paper's AST dump already prints. `data`
and `vtable` are private members of `DynRef`, and the literal is in a
namespace-scope function. This works because rule 6 checks access as the
injected member function (a member of `DynRef`), not from the literal's
location.

```diff
 template<class Iface> class DynRef {
     void *data;
     consteval {
         inject_Vtable(^^Iface);
-        inject_vtable_for(^^Iface);
+        inject_vtable_for(^^Iface, ^^DynRef);
         inject_satisfies_for(^^Iface);
     }

 public:
     consteval {
         inject_interface(^^Iface);
-        inject_erasing_ctor();
+        inject_erasing_ctor(^^DynRef);
     }
```

`inject_Vtable` declares `VTable` and `vtable` (declarator-ids, rule 2) and
uses nothing from the target. `inject_satisfies_for` uses only `std::` names
and names it declares. Neither changes.

#### Bindings (§2.5, §2.9)

Injected into `std`: the class-head names `tuple_size`/`tuple_element` belong
to the target (rule 2), but names used in the base clause are ordinary uses
and resolve from the literal's scope (`lib`):

```diff
             template <class... Ts>
             struct tuple_size<\(tmpl)<Ts...>>                       // class-head: target
-                : integral_constant<size_t, size(\(tmpl)<Ts...>::tuple_elements)>
+                : std::integral_constant<std::size_t, std::size(\(tmpl)<Ts...>::tuple_elements)>
             { };

-            template <size_t I, class... Ts>
+            template <std::size_t I, class... Ts>
             struct tuple_element<I, \(tmpl)<Ts...>> {              // class-head: target
```

`get` injected into the user's namespace:

```diff
-            template <size_t I, ::lib::specializes<\(tmpl)> Self>
+            template <std::size_t I, specializes<\(tmpl)> Self>
```
Dropping `::lib::` is now safe: `specializes` resolves from the literal's
scope (`lib`), and nothing in the user's namespace can take it over.

`get` injected as a member (`inject_members` version):

```diff
-                return (((Self&&)self).[: tuple_elements[I] :]);
+                return (((Self&&)self).[: self.tuple_elements[I] :]);
```
That's the spelling the paper's first version already used.

#### Summary for §2

| Example | Injected into | Target names used | Change needed |
|---|---|---|---|
| 2.1 Type erasure | `DynRef<I>` | `DynRef` (in `requires`), `satisfies_interface`, `vtable_for`, `VTable`, `data`, `vtable` | `\(cls)::`, `this->`; pass `^^DynRef` |
| 2.2 Formatting | `std` | `formatter` (class-head, rule 2) | none |
| 2.3 Literal testing | `main` block | none (literal and injection both in `main`) | none |
| 2.4 Iterator interface | user class | already uses `\(cls)` and `this->` | none (see §9, item 6) |
| 2.5 / 2.9 Bindings | `std`, user namespace, user class | `integral_constant`, `size_t`, `size`, `tuple_elements` | qualify with `std::`; `self.tuple_elements` |
| 2.6 `Eq` | user class | none | optional `__lhs` → `lhs` |
| 2.7 LoggingVector | the class itself | `impl` (literal is written in the class) | optional `::log_call` → `log_call` |
| 2.8 Mocking | the class itself | `calls_` (literal is written in the class) | none |

In §2.7 and §2.8 the literals are written inside the class, so the literal's
scope already is the target and unqualified member names just work.

Every change in this table turns a hidden dependency on the injection target
into an explicit one. That arguably documents the generator better: reading
`inject_erasing_ctor` today, you can't tell which names it expects the target
to provide.

Rule 3 matters in §2 as well:
- Type erasure builds `^^{ void const* obj }` and `^^{ T const* }` /
  `^^{ T const }` in separate literals and splices them into a literal that
  declares `template <class T>` and uses `obj`.
- `DeriveDebug` declares `out` in one literal and uses it in others.
- `Eq` builds `(__lhs.\(s) == __rhs.\(s))` pieces separately from the
  function that declares `__lhs`/`__rhs`.

All of these need hygiene to be per injection, not per literal.

### 8.3 Summary of effects on the paper

- **Must change:** about ten spots in §2.1, §2.5 and §2.9 that use members
  of the injection target (`\(cls)::`, `this->`, `std::` qualification,
  `self.tuple_elements`), plus passing `^^DynRef` to two §2.1 generators.
  Nothing in §3.
- **Can be simplified:** defensive `::` qualifications (including
  `::lib::specializes`), `\(^^T)` → `T`,
  `\(sizeof...(Args))` → `sizeof...(Args)`, `__`-prefixed locals, possibly
  the `clone_naming` prefixes (§9, item 5).
- **Latent bugs fixed silently:** unqualified `fwd!` in `check!`, `try_!`
  and `define_op!`; `std::get` in `my_tuple`.
- **Newly possible:** overloaded helpers; `detail` helpers; private helpers
  and data for member macros (the logging example's `do_log`/`min_level`,
  `ranges::begin`'s `adl_begin`), or for befriended macros (rule 6).
- **Newly diagnosable:** forgotten interpolations of macro locals.
- **Prose to update:** §3.1's `T&&` warning; §3.4's "necessarily unhygienic"
  (now explicit capture via `id()`); §3.2's "partially hygienic".

---

## 9. What the paper would need to specify

1. **The expansion unit (rule 3).** Hygiene must be per injection or per
   macro result, not per literal (type erasure, `DeriveDebug`, `Eq`). Each
   token carries two separate things: the scope it was written in (from its
   literal) and which expansion it belongs to (stamped at injection). Tokens
   from `token_sequence` arguments are marked as user-origin and don't get
   the expansion stamp.

2. **Macro-body local variables referenced from literals.** `text`, `sloc`,
   `ops`, `level` and the parameter `self` are runtime values of the
   *macro's* evaluation. If a literal identifier resolves to one of them,
   make it ill-formed (Racket calls this a phase error) with a "did you mean
   `\(x)`" diagnostic. Allow types, templates and template parameters (`T`,
   `sizeof...(Args)`). Whether `constexpr` locals like `level` can be named
   directly, like non-odr-uses in lambdas, is a separate question.

3. **Lookup order.** The output's own scopes (expansion-local declarations)
   win, then the literal's scope. The logging macro's
   `auto& self = \(self); … self.min_level` is the example: the generated
   `self` must win over the macro parameter `self`.

   **Getting hold of the target.** Rule 2 makes generators name the
   injection target explicitly (`\(cls)::VTable`). Passing a reflection, as
   `iterator_interface` does and as the revised §2.1 does, works today. A
   library query for "the class or namespace this `queue_injection` will
   land in" would save the plumbing, much like `macro_expansion_context()`
   does for macros.

4. **Nested literals.** The logging macro's inner `^^{…}` is part of the
   outer literal's tokens but is parsed inside the injected macro body. A
   single "where was I written" pointer suggests the outer scope. Racket's
   sets-of-scopes model accumulates both. The logging example is the test
   case. This only affects **lookup**: access follows rule 6 (the macro being
   expanded), whichever answer is chosen.

5. **Names generated by the API.** `declaration_of` invents `p0`/`T0`, and
   `forwarding_call_for`, `argument_list_for` and
   `template_parameter_list_for` refer to them. Those tokens need a defined
   context. The appealing option is to make them *fresh* and mutually
   consistent. Then the `clone_naming` prefixes, which exist to avoid
   collisions (§2.7), are only needed when the author wants to write `p0` in
   a literal, and then `id()` is the tool.

6. **Using-directives in the generator now affect the output.**
   `iterator_interface` begins with `using namespace std::meta;`. Under rule
   1 that directive is part of the literal's scope, so unqualified names in
   the generated operators can find `std::meta` names. None of the current
   tokens collide, but it's new behavior that needs a sentence.

7. **Debugging output.** With `stringize` (§2.10), two `lhs` tokens that now
   mean different things print identically. A debug printer would want to
   show the binding (`lhs#1`, or source arrows as DrRacket draws them). This
   ties back to Pombrio's chapter 4 origin tracking.

8. **Snapshot vs. live lookup.** Does a recorded scope see declarations added
   after the literal but before injection? "Live, like a template's point of
   instantiation" is the natural answer, and §2.1's deferred-lookup problem
   already assumes it.

9. **Helper functions that build fragments.** A `consteval` function in
   `mylib::detail` returning `^^{ helper(...) }` records its own scope, not
   its caller's. That's usually right, and the `id()` escape covers the
   exceptions. It needs to be spelled out.

10. **Moving tokens between contexts.** A user's `token_sequence` argument
    stored and spliced into a class-scope injection keeps the call site's
    context. That's correct, but the wording should say so.

11. **Control-flow hygiene (separate from names).** `break`/`continue`/
    `return`/`co_await` in arguments that a macro wraps in a loop, lambda or
    `try`; exceptions a macro catches that user code might also throw.

12. **Token context through `token_sequence` operations.**
    - Indexing (`body[i]`), subranges, iteration, concatenation (`+`, `+=`)
      and `list_builder` must carry each token's context unchanged.
      Macros like `fun!` and `define_op!` take user arguments apart and
      reassemble them, and they only stay correct if a user token remains
      a user token.
    - Tokens produced by `id()`, `str_lit()` and `tokenize()` are
      context-free, i.e. looked up at the injection site (rule 4).
      `stringize` followed by `tokenize` therefore strips context, which is
      a legitimate but explicit way to opt out.

13. **What `==` means on identifier tokens.** `poem[0] == ^^{ if }` works
    today because it compares spelling. Once tokens carry context,
    `tok == ^^{ e }` compares a user token with a macro-written one. `==`
    should remain **spelling equality**, which is what macros that parse
    their input (`fun!`, `define_op!`, `λ!`'s placeholder scan) need. If
    binding-aware comparisons are ever wanted, they should be separate
    functions. Racket separates `bound-identifier=?` ("would one bind the
    other?") and `free-identifier=?` ("do they refer to the same thing?")
    from plain symbol equality.

14. **Access (rule 6).** Specify:
    - Tokens in the body of an injected declaration are access-checked as
      that declaration's entity.
    - Macro-written tokens in an expansion are checked as the macro being
      expanded, including helper-built fragments: the macro vouches for all
      of its macro-written output, wherever the literal was written.
    - User-written tokens, and typed arguments, are checked as the user.
    - Macros can be befriended (`friend __macro check;`, placeholder
      syntax), and a member macro has its class's access.
    - The guarantee to state: a macro can use what it has access to, and its
      arguments can't gain access through it.

Items 1, 2 and 5 carry most of the design weight.

---

## 10. References

- Justin Pombrio. *Resugaring: Lifting Languages through Syntactic Sugar.*
  PhD dissertation, Brown University, 2018.
  <https://cs.brown.edu/media/filer_public/b4/07/b4073414-22e3-44af-812f-97d4239b4ef9/pombriojustin.pdf>
  - §1.3 when to use sugar; §2 taxonomy (Table 1); §2.1.7 hygiene; §3.2.1
    restrictions; ch. 4 evaluation resugaring; §5.3.4 relation to sets of
    scopes; §5.5.4–5.5.5 hygiene as α-equivalence preservation; §6.4.3 fresh
    variables and capture sets; §6.6.2 `foreach`; Figures 17, 18, 20–23.
- Pombrio & Krishnamurthi. *Resugaring: Lifting Evaluation Sequences through
  Syntactic Sugar.* PLDI 2014. *Hygienic Resugaring of Compositional
  Desugaring.* ICFP 2015.
- Pombrio, Krishnamurthi & Wand. *Inferring Scope through Syntactic Sugar.*
  ICFP 2017.
- Pombrio & Krishnamurthi. *Inferring Type Rules for Syntactic Sugar.*
  PLDI 2018.
- Matthew Flatt. *Binding as Sets of Scopes.* POPL 2016.
- Eli Barzilay, Ryan Culpepper & Matthew Flatt. *Keeping it Clean with Syntax
  Parameters.* Scheme Workshop 2011.
- Kohlbecker, Friedman, Felleisen & Duba. *Hygienic Macro Expansion.* 1986.
- Clinger & Rees. *Macros that Work.* POPL 1991.
- Michael D. Adams. *Towards the Essence of Hygiene.* POPL 2015.
- P4380R0, *Token Sequence Injection* (Revzin, Vandevoorde, Alexandrescu).
  <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4380r0.html>
- P3294R2, *Code Injection with Token Sequences.*
