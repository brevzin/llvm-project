# Statement parameters and expansion functions (sketch)

Motivated by `each!` (`libcxx/test/std/experimental/reflection/each-loop.pass.cpp`)
and by how Jai does custom iteration (`for_expansion`). Nothing here is
implemented.

## The problem `each!` exposed

`each!` gives optimal loops over view pipelines, push-style views written like
generators, and a body in which `break`, `continue`, `return` and `co_yield`
all mean what they would in a `for` loop. But nearly all of its code is
working around one thing: **the body is a token sequence, with no scope and no
structure**.

| `each!` does | because tokens don't know |
|---|---|
| scans view tokens for `co_yield` | where the insertion points are |
| `rewrite_breaks`, `goto done`, `do { } while (false)` | which loop a `break`/`continue` belongs to |
| `unique_id` for every local, in views too | scopes: the view's `e` captures the consumer's `e` |
| pastes the user's `int i = E;` | that the binding is the caller's declaration |
| `lookup("__func_")`, `lookup("__views_")` | access: the loop is written outside the class |
| nested macros, re-parsing, ~54 ms per 4-layer loop | anything: every level is lexed and parsed again |

Jai has the same feature with none of these problems. Its `for` calls a
macro, `for_expansion(container, body: Code, flags)`, where the body is
parsed code with the caller's scope attached. The macro's locals are private
by default, `` `it `` declares into the caller's scope, `#insert body;` places
the body, and `#insert (break=..., continue=..., remove=...)` lets the macro
redefine those statements for the inserted code.

## The design: two pieces

1. A **statement parameter**: a function parameter whose argument is a
   statement, written by the caller and analyzed in the caller's scope, which
   the callee may *insert*.
2. An **expansion function**: a function whose body is instantiated into the
   caller at each call, as a statement, instead of being called. Only an
   expansion function can have statement parameters.

The range-`for` loop is then one use of them: `operator for` is an expansion
function, and the loop body is its statement parameter.

The spellings below (`__expand`, `__stmt`) are placeholders.

### Expansion functions

```cpp
void with_lock(std::mutex& m, __stmt body) __expand {
  std::lock_guard g(m);
  body();                   // insert the statement here
}

int f(std::mutex& m) {
  with_lock(m) {            // a trailing statement is the statement argument
    if (ready()) return 1;  // returns from f
  }
  return 0;
}
```

- An expansion function is a template in everything but name: it must be
  defined where it is used, it's instantiated once per call, and it has no
  address, no linkage, no ABI. A call `with_lock(m) S` is a statement that
  means the expansion function's body, with its parameters as local variables
  initialized from the arguments, and `S` at each insertion.
- Its body is ordinary code. Names in it are looked up where it's defined, by
  the usual rules. Its locals are its own: `g` can't be seen by, or capture
  anything in, `S`. Access is checked as a member of its class. None of
  `unique_id`, `\(^^std::move)`, or `lookup("__func_")` is needed.
- `return;` in an expansion function leaves the expansion (the whole call
  statement). Its own loops' `break` and `continue` are its own.
- A statement argument can be written after the call (`f(args) S`) when the
  last parameter is a statement parameter. In statement position, `f(x) {` is
  otherwise ill-formed when `f` doesn't name a type (if it does, `S(x) {...}`
  declares `x`), and an expansion function isn't a type.

### Statement parameters

`__stmt body` takes a statement; `__stmt(N) body` takes a statement with `N`
*bindings*, values each insertion supplies: `body(e)` inserts the statement
with the binding initialized from `e`. Who declares the bindings is the
caller (for a loop, `for (int i : r)`), so the callee only says how many
values it supplies.

Inside the inserted statement:

- `continue` ends this insertion: control goes on in the expansion function,
  right after `body(e)`.
- `break` ends the *statement that received the body* (for a loop: the `for`
  statement), however many insertions and expansion functions are in between.
  The target belongs to the body, not to the insertion, so forwarding the body
  on to another expansion function doesn't change it.
- `return`, `co_yield`, `co_await`, and `goto` to the caller's labels are the
  caller's: the statement is caller code.
- Names are the caller's. A binding is a local of the statement, declared by
  the caller.

A statement parameter can be inserted any number of times (zero, once,
twice: `concat`), and forwarded to another expansion function's statement
parameter. It can't escape: no copying it, storing it, or inserting it inside
a lambda or local class in the expansion function, since its jumps can't
cross a function boundary. (This is CLU's rule: `yield` only lexically inside
the iterator.)

#### When bindings are `auto`

`for (auto&& x : r)` binds a type only the insertion knows, and different
insertions may supply different types (a `concat` of a `vector<int>` and a
`vector<string>`). So a statement with bindings is a *pattern*, like a
generic lambda's body or an expansion statement's: analyzed as a template at
the caller, instantiated at each insertion with the binding's type. That gives
hygiene by C++'s own rule, two-phase lookup: non-dependent names are bound in
the caller's scope when the loop is parsed; dependent ones also get ADL at the
insertion.

This also means a heterogeneous `concat` needs no common reference type: each
insertion is its own instantiation.

## Range-`for`

`for (D : r) S` uses `r`'s `operator for` if it has one (member, or found by
ADL), and iterators otherwise. It means

```cpp
{
  auto&& __r = r;
  __r.operator for(S-with-binding-D);   // break in S: past this block
}
```

### The standard views

```cpp
template <class V, class P>
class filter_view {
  V base_;
  P pred_;
public:
  template <std::direction Dir = std::forward>
  void operator for(this auto&& self, __stmt(1) body) __expand {
    for <Dir> (auto&& e : std::forward_like<decltype(self)>(self.base_))
      if (self.pred_(e))
        body(e);
  }
};

template <class V>
class reverse_view {
  V base_;
public:
  template <std::direction Dir = std::forward>
  void operator for(this auto&& self, __stmt(1) body) __expand {
    for <!Dir> (auto&& e : std::forward_like<decltype(self)>(self.base_))
      body(e);
  }
};

template <class V, class P>
class take_while_view {
  // ...
  template <std::direction Dir = std::forward>
  void operator for(this auto&& self, __stmt(1) body) __expand {
    if constexpr (Dir == std::forward) {
      for (auto&& e : std::forward_like<decltype(self)>(self.base_)) {
        if (!self.pred_(e)) return;     // leaves the expansion
        body(e);
      }
    } else {
      auto&& base = std::forward_like<decltype(self)>(self.base_);
      auto stop = std::ranges::find_if_not(base, self.pred_);
      for <Dir> (auto&& e : std::ranges::subrange(std::ranges::begin(base), stop))
        body(e);
    }
  }
};

template <class... Vs>
class concat_view {
  std::tuple<Vs...> views_;
public:
  template <std::direction Dir = std::forward>
  void operator for(this auto&& self, __stmt(1) body) __expand {
    // (backward: the tuple in reverse order -- elided)
    template for (auto&& v : std::forward_like<decltype(self)>(self.views_))
      for <Dir> (auto&& e : v)
        body(e);                         // one insertion per range
  }
};
```

`for <Dir>` is a stand-in for "iterate in direction `Dir`": a range-`for` that
passes `Dir` to the range's `operator for`, or uses its iterators backward.
How direction is spelled is open (below); `each!` passes it as a parameter,
Jai as `For_Flags` chosen by `for <`.

Composition is just nesting: `filter_view::operator for` iterates its base
with a range-`for`, which uses the base's `operator for`. So the whole
`take_while | filter | reverse` pipeline instantiates into one loop, with the
call counts `each!` gets (5 and 4, not 8 and 10), and any function that
contains a range-`for` (a `find`, a `fold`) gets them too.

What a user writes is the same thing, which is the point: no `loop_traits`,
no tokens.

```cpp
template <class V>
struct join {
  V base;
  template <std::direction Dir = std::forward>
  void operator for(this auto&& self, __stmt(1) body) __expand {
    for <Dir> (auto&& inner : self.base)
      for <Dir> (auto&& e : inner)
        body(e);     // a break in the body leaves both loops: it's the body's
  }
};
```

## Relation to `__macro`

They're complementary.

| | `__macro` | expansion function |
|---|---|---|
| is | a code generator (consteval, returns tokens) | code (a template instantiated into the caller) |
| checked | when expanded | when defined, like any template |
| names | looked up at the expansion | looked up where defined |
| can make | anything: declarations, names, syntax | statements; bindings declared by the caller |
| hygiene | none by default (`unique_id`) | by construction |

A bridge, useful on its own: let a `__macro` take a `__stmt` parameter too,
interpolated as one opaque token carrying the analyzed statement (as an
expression parameter is today). Then a generated loop gets a body whose names
are already bound and whose `break`/`continue` the compiler resolves -- the two
problems `each!` works around with `unique_id` and `rewrite_breaks`.

## Implementation sketch (clang)

- `__expand` function templates: parsed and checked as templates. A call is
  resolved as usual, then instantiated not as a `FunctionDecl` but as an
  `ExpansionStmt` in the caller: parameters become locals, the body is
  transformed (`TreeTransform`, as for instantiation) into the caller's
  function, with the expansion function as the context for access checks.
  `return` becomes a jump to the end of the `ExpansionStmt`.
- A statement argument is analyzed at the caller as a pattern (a dependent
  context for its bindings), like a generic lambda's body or an expansion
  statement's -- clang's expansion statements already instantiate a body per
  element with different types.
- `body(e)` instantiates the pattern: `{ D = e; S }`, with `continue` bound to
  the end of this instance and `break` to the end of the statement that
  received the body.
- Recursion (a tree whose `operator for` loops over its children) instantiates
  forever: diagnose at a depth limit (Jai: 1000).
- CodeGen: nothing new; debug info marks the expansion as inlined at the call.

## Open questions

1. Spelling: `__expand` and `__stmt(N)`, or something closer to existing
   syntax (`inline` + a `statement` placeholder).
2. Direction (and other loop options): a template parameter of
   `operator for`, flags as in Jai, or a separate `operator for` overload
   taking a tag. And how a loop asks for it: `for <reverse> (...)`?
3. Bindings outside `for`: `with_lock(m) S` has none; a general call that
   supplies values needs a way for the caller to name them (Kotlin
   `{ x -> ... }`, Ruby `|x|`).
4. Redefining `break`/`continue` at an insertion, as Jai's
   `#insert (break=..., continue=...)`, and container-defined statements like
   Jai's `remove`. Not needed by any view above.
5. Non-`void` expansion functions: a call would be an expression that can
   leave by `return`/`break` from the caller (as a statement expression can).
6. Interaction with existing range-`for`: adding `operator for` to the
   standard views changes how often a predicate is called. Predicates are
   required to be equality-preserving, but the difference is observable.
7. Iterators stay: this is internal iteration. `zip`, lockstep iteration, and
   pausing still need them.
