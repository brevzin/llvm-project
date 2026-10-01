# Diverging expressions: proposal summary (post-review)

This builds on P3549R1 and `ideas/diverging.md`. It is an abbreviated
statement of the proposal, not CWG wording.

## The type `noreturn_t`

- `noreturn_t` is a new fundamental type, next to `void` and
  `std::nullptr_t`. The library names it as
  `namespace std { using noreturn_t = decltype(throw 0); }` in `<cstddef>`.
- It is a complete object type that has no values.
  - `sizeof(noreturn_t) == 1`. Whether it could be 0 is deliberately left
    out of scope.
  - It has no default constructor.
  - Copy, move and destruction are trivial, so it is trivially copyable.
    `variant`, `expected` and `optional` therefore stay trivial when they
    contain it.
  - It is not an implicit-lifetime type, so no implicit object creation.
  - It cannot be the target type of `bit_cast`.
  - A glvalue of type `noreturn_t` can only designate an object that does
    not exist. `reinterpret_cast` or a pointer cast may form one; accessing
    it is UB.
- New primary type category trait: `is_noreturn` (precedent:
  `is_null_pointer`). It is fundamental but not scalar.

## Conversions

- A `noreturn_t` (any value category) converts implicitly to any type `T`,
  including `void`, cv-qualified types and reference types.
  - This is its own standard conversion with **Exact Match** rank.
  - It never goes through `T`'s constructors.
  - With several viable overloads the call is ambiguous, which is accepted.
- Conditional operator: the special bullet stays, generalized from "a
  throw-expression of type `void`" to "an operand of type `noreturn_t`".
  - The result is the other operand, keeping its type, value category and
    bit-field-ness. `c ? x : throw 1` stays an lvalue.
  - If both operands are `noreturn_t`, the result is a `noreturn_t` prvalue.
  - This gives `common_type<T, noreturn_t> = T` and
    `common_reference<T&, noreturn_t&> = T&`, which the valueless-ranges use
    case relies on.
  - It also fixes `c ? x : (log(), throw 1)`.
- Function pointers:
  - `noreturn_t(*)(Args...)` converts to `void(*)(Args...)` unconditionally.
    A `noreturn_t` return has the same ABI as a `void` return.
  - It converts to `R(*)(Args...)` for any other `R` only if the source is a
    constant expression; the implementation emits a thunk.
  - The rationale to give in the paper is hidden-return-pointer (sret)
    argument shifting on x86-64 SysV, Win64 and x86. A non-returning callee
    never writes return registers, so those are not the issue. AArch64
    passes the hidden pointer in x8, so it has no problem.
  - This bullet is severable.
- Covariant overrides: `noreturn_t f() override` may override `virtual R f()`
  for any `R`. Compilers already emit thunks for covariant returns.
- `throw` expressions have type `noreturn_t` (was `void`).

## Diverging expressions and statements

- An expression *diverges* if:
  - its type is `noreturn_t`, or
  - it is a call to a function declared `[[noreturn]]`, whatever its
    declared return type. For divergence and deduction it is treated as if
    its type were `noreturn_t`; its `decltype` does not change.
- A statement *diverges* if it is:
  - a compound statement whose last statement diverges,
  - an `escape-statement` (use this spelling everywhere),
  - an expression statement whose expression diverges,
  - a declaration statement where some declarator's initializer diverges,
  - a `return` or `do_return` whose operand diverges,
  - an `if` with an `else` where both substatements diverge,
  - an `if constexpr` whose taken substatement diverges.
- Deliberately not covered, so a false negative means writing `-> T`:
  - a `switch` where every case diverges (with a `default`),
  - `try` blocks,
  - `for (;;)` and `while (true)` without `break`,
  - labeled statements,
  - a diverging statement that is not last.

## Type of a `do` expression

- If there is a non-placeholder trailing return type, that type.
- Otherwise, deduce from the `do_return` statements, ignoring those whose
  operand diverges. If the remaining ones disagree, the program is
  ill-formed.
- Otherwise (no `do_return` statements, or only diverging ones), if the
  *compound-statement* diverges, `noreturn_t`. A `do_return` whose operand
  diverges counts as a diverging statement here.
- Otherwise, `void`.
- Changed from the first draft, which said "if every `do_return` operand
  diverges, the type is `noreturn_t`". That turned
  `do { if (c) do_return fatal(); }` from a valid `void` expression into an
  error, because a `noreturn_t` do-expression must not fall off the end.
  Diverging `do_return`s now simply take no part, as if they were absent.

## Return type deduction for functions and lambdas

- Use the same rules as for `do`. `return` statements whose operand diverges
  are ignored. If none remain, a diverging body (where a `return` of a
  diverging operand counts as diverging) deduces `noreturn_t`, and otherwise
  `void`. So `auto f(bool c) { if (c) return fatal(); }` returns `void`.
- This makes `auto fatal = [](auto&&... a) { log(a...); std::abort(); };`
  compose.
- It is a breaking change: `decltype(f())` goes from `void` to `noreturn_t`.
  The unconditional conversion to `void(*)` covers most uses of the address.
  Itanium mangles the declared `auto`, not the deduced type.
- Flowing off the end of a function returning `noreturn_t` is already UB
  under the rule for non-void functions. `[[noreturn]]` semantics fall out.

## Library

- C++-only `[[noreturn]] void` functions now return `noreturn_t`: `terminate`,
  `unreachable`, `rethrow_exception`, `throw_with_nested`, and so on.
- C library functions keep `[[noreturn]] void`, because they must stay the
  same entities as the C declarations: `abort`, `exit`, `quick_exit`,
  `_Exit`, `longjmp`. They diverge through the `[[noreturn]]` rule.
- No specializations are required. `noreturn_t` is complete, so
  `expected<T, noreturn_t>` and friends work with the primary templates. That
  includes `expected<T, E>`'s converting constructor, which calls
  `other.error()`. Compact layouts are QoI or a later paper.

## Paper changes

- Lead the `[[noreturn]]` comparison with: types may affect well-formedness,
  standard attributes (by EWG policy) may not.
  - Present the `[[noreturn]]`-diverges rule as transitional.
  - Ask EWG to explicitly accept that removing `[[noreturn]]` can make
    `int x = c ? 1 : fatal();` ill-formed.
- Use `std::unreachable()` as the headline example.
- Add `do { if (c) throw 1; std::terminate(); }` to the examples.
- Acknowledge that the type removes only the expression half of the
  divergence property. Statements still need it.
- Parameters of type `noreturn_t` are fine: `template <class T> void g(T)`
  with `g(throw 1)` deduces `T = noreturn_t`, and the argument diverges
  before the call.

## Open: operators and declarations with `noreturn_t` operands

Example: `int x = 1 + std::unreachable();`

1. **As currently specified it does not type-check; it is ambiguous.**
   - The built-in candidates for `+` include `LR operator+(L, R)` for every
     pair of promoted arithmetic types.
   - With an Exact Match conversion from `noreturn_t`, every `(int, R)`
     candidate is equally good. The same holds for unary `-`, `*p`,
     subscripting, and so on.
   - Leaving it ambiguous is defensible for hand-written code. It is bad for
     generic code, where `x + f()` with `f` deducing `noreturn_t` produces a
     confusing error.

2. **Proposed fix: absorption, decided before overload resolution.**
   - Rule: if an unconditionally evaluated operand of an operator expression
     has type `noreturn_t`, the expression is a prvalue of type
     `noreturn_t`. There is no operator lookup or overload resolution, for
     built-in and user-declared operators alike.
   - This is sound. Every operand of an operator function call is evaluated
     before the function is entered, so no operator function could ever run.
   - "Unconditionally evaluated" means every operand except the second
     operand of `&&` and `||` (keep this even if they turn out to be
     overloaded). The conditional operator keeps its own bullet.
   - `1 + std::unreachable()` has type `noreturn_t` and diverges through the
     type rule. `int x = ...` still works through the conversion, so
     divergence stays a property of the type.
   - It chains: `std::cout << "x" << std::unreachable() << "y"` is
     `noreturn_t`.
   - Comma falls out: `(throw 1, 5)` is `noreturn_t`.
     `c && std::unreachable()` stays `bool`, since that operand may not be
     evaluated.
   - Rejected alternative: run overload resolution and absorb only when no
     candidate is viable (or when built-ins are ambiguous). Then
     `"hello"s + std::unreachable()` depends on library details.
     - Before C++26, every `operator+` for `basic_string` deduces its second
       parameter, and deduction fails against `noreturn_t`. Nothing is
       viable, so the result is `noreturn_t`.
     - C++26 (P2591) added `operator+(basic_string&&,
       type_identity_t<basic_string_view>)`. Its second parameter is a
       non-deduced context, so `noreturn_t` converts and the result is
       `std::string`, which does not diverge.
     - Whether an expression diverges must not depend on which overloads
       happen to exist.
   - Costs:
     - `s + std::unreachable()` is well-formed even if `S` has no
       `operator+`.
     - `decltype("hello"s + std::unreachable())` is `noreturn_t`.
     - `decltype((throw 1, 5))` changes from `int` to `noreturn_t`.
     - A requires-expression such as `{ t + std::unreachable() }` is always
       satisfied.
     - Today any operand of type `void` is ill-formed, so the comma operator
       is the only existing code whose meaning changes.

3. **Function calls and constructions with a diverging argument do not
   propagate (open: reconsider).**
   - `foo(std::unreachable())` has `foo`'s return type and does not diverge.
   - With absorption decided before overload resolution, this is a sharper
     asymmetry: `s + std::unreachable()` diverges but
     `operator+(s, std::unreachable())` does not.
   - Absorbing for calls the same way is simple to state and sound for the
     same reason (arguments are evaluated before entry). But it skips
     checking the call, so `f(1, 2, std::unreachable())` would be accepted
     with the wrong arity or no `f` at all, and the same applies to
     `T(std::unreachable())` and `T{...}`.
   - Propagating would need a separate "unconditionally evaluates a
     diverging subexpression" property over arbitrary expressions. That
     means arguments, initializers and member access, excluding unevaluated
     operands, lambda bodies and default arguments. It is the
     extra-property design the type approach avoids.
   - The statement rules cover the realistic case, because people put the
     diverging call last. This can be a later extension.

4. **Declarations whose initializer diverges (decided: yes).**
   `int x = 1 + unreachable();` is a declaration statement. A
   simple-declaration where some declarator's initializer diverges is a
   diverging statement (added to the list above).
   Initialization happens in order, so a diverging initializer means control
   never passes the declaration.

5. **`throw` with a `noreturn_t` operand (decided).** The paper's own
   workaround `throw (std::terminate(), 0)` becomes `throw <noreturn_t>`
   through the comma rule. Such a throw-expression is well-formed and is
   just its operand: the operand diverges before any exception object could
   be created, so none is, and the whole expression has type `noreturn_t`
   like any other throw-expression. Without this the idiom would break.

6. **Variables of type `noreturn_t` (decided: allowed).**
   - `auto x = throw 1;` is ill-formed today because it would declare a
     `void` variable. With this proposal it declares a `noreturn_t`
     variable, and absorption makes `auto y = 1 + unreachable();` do the
     same.
   - Allowed. The initializer never completes, so the object
     never exists. Guaranteed copy elision means no constructor is needed.
   - A `noreturn_t` variable without a diverging initializer is ill-formed,
     since there is no default constructor.

## Implementation status (prototype in this fork)

Enabled by `-fdiverging-expressions`, on by default in `-std=c++2d`. The
feature-test macro is `__cpp_diverging_expressions` (1) and
`__has_feature(diverging_expressions)` is true.

### Done

- **The type.** `BuiltinType::NoReturn` is spelled `decltype(throw 0)`.
  - `sizeof` 1. Fundamental, object, literal, trivially copyable, not scalar,
    not implicit-lifetime.
  - Default/value initialization is an error, and so is initializing it from
    anything other than a `noreturn_t`.
  - It cannot be the target of `__builtin_bit_cast`.
  - Itanium mangling is `u10noreturn_t`.
- **Conversions.**
  - Conversion to any type, including references, class types without
    usable constructors, and `void`, is a new standard conversion
    `ICK_NoReturn_Conversion` with Exact Match rank, implemented with cast
    kind `CK_NoReturnToAny`.
  - `g(throw 1)` with two viable overloads is ambiguous.
- **Function pointers.** `noreturn_t(*)(Args...)` to `void(*)(Args...)` is a
  function-pointer conversion.
  - In the ABI, a `noreturn_t` return is lowered as void and the function is
    `noreturn`; a `noreturn_t` parameter takes no slot.
- **Conditional operator.** A diverging operand (type `noreturn_t` or a
  `[[noreturn]]` call) yields the other operand, with its type, value
  category and bit-field-ness. Two diverging operands yield `noreturn_t`.
- **Absorption.** For `+`, unary operators, `=`, `op=`, `[]`, `,`, and the
  left operand of `&&`/`||`, the decision is made before any operator lookup.
  This covers templates and `CreateOverloaded*` during instantiation.
- **Statements and `do` expressions.**
  - `isDivergingStmt` covers the statements listed above plus declaration
    statements with a diverging initializer.
  - In a `do` expression, `do_return` statements with diverging operands take
    no part in deduction. If no other `do_return` remains, the type is
    `noreturn_t` when the body diverges and `void` otherwise.
- **Return type deduction for functions and lambdas** uses the same rule.
  - `void f() { return fail(); }` is allowed.
  - `-Winvalid-noreturn` is not issued for returning a diverging expression.
- **Throw and diagnostics.**
  - `throw` with a `noreturn_t` operand is just that operand.
  - `-Wunreachable-code` does not report the conversions, absorbed operators,
    or `return`s that follow a diverging operand.
  - `decltype(throw 0)` does not warn about side effects in an unevaluated
    context.
- **Constant evaluation.** Evaluating a diverging expression reports the
  reason it fails (the throw, or a non-constexpr call).
- **libc++** (only with `__cpp_diverging_expressions`):
  - `std::noreturn_t` in `<cstddef>`.
  - `std::is_noreturn[_v]`.
  - `std::unreachable()` and `std::terminate()` return `noreturn_t`.
    `terminate` keeps the same ABI and mangling as the runtime's
    `void terminate()`.
- **Tests:**
  - clang/test/SemaCXX/cxx29-diverging-expressions.cpp
  - clang/test/CodeGenCXX/cxx29-diverging-expressions.cpp
  - libcxx/test/std/language.support/support.types/noreturn_t.pass.cpp

### Not done

- Conversion of a *constant* `noreturn_t(*)(Args...)` to `R(*)(Args...)` for
  other `R` (the thunk).
- Covariant overrides with a `noreturn_t` return.
- Other library functions: `rethrow_exception`, `throw_with_nested`.
- Pattern matching. The fork has no `match`; conditional, `do` and functions
  exercise the same rules.
- `(c ? s.bitfield : throw 1) = 2` type-checks but CodeGen rejects it ("cannot
  compile this conditional operator yet"). Clang rejects it today too.
