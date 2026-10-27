# Reflecting overload sets: decisions and implementation

Companion to `overload-sets.md`. What was decided, and how the fork implements
it.

## Semantics

- `^^f`, for `f` naming two or more functions and function templates, reflects
  that overload set as lookup finds it at that point (a snapshot). This works
  for unqualified names, qualified names (`^^N::f`, `^^C::m`), a member named
  inside a member function (implicit `this->`), a using-declaration bringing
  several functions, and `^^operator==`.
- A name that resolves to one function is still that function's reflection,
  as in C++26. More generally, one candidate is *always* reflected as itself,
  never as a set: `make_overload_set({^^g}) == ^^g`, `overloads_of(^^g) ==
  {^^g}`. There is exactly one representation of each thing.
- `^^f<int>` with explicit template arguments still has to resolve to one
  specialization. (An overload set with template arguments bound would be a
  separate feature.)
- **Identity is set identity.** A set is its candidates' canonical
  declarations, without duplicates, in declaration order. Two reflections
  compare equal if and only if they have the same candidates, whatever the
  order, duplicates, or nesting that built them. The candidates are the
  functions themselves, never a using-declaration's shadow, so `^^u` (after
  `using A::u;`) `== ^^A::u`, and `make_overload_set(overloads_of(r)) == r`.
- **Splicing does overload resolution among exactly the candidates.**
  - There is no argument-dependent lookup and (as for any splice) no access
    check.
  - Calls, explicit template arguments (`template [: r :]<int>(x)`, where only
    the function templates are candidates), and address-of with a target type
    (`int (*p)(int) = [: r :];`) all work.
  - `obj.[: r :](args)` and `ptr->[: r :](args)` resolve among member
    candidates, static and non-static.
  - `[: r :](args)` whose candidates are all non-static members is an error
    ("no implicit `this`"), as for one member function.
- **Members from a base, as with a using-declaration.** In member call syntax,
  each candidate's implicit object parameter is of the *most derived* class
  among the candidates' classes. This is what [over.match.funcs] does for a
  member a using-declaration brings into a derived class.
  - Without it, `struct E : B { using B::f; int f(double); }` would make
    `e.[: ^^E::f :](1)` ambiguous (`B::f(int)` needs an `E`→`B` object
    conversion), although `e.f(1)` isn't.
  - It never changes ordinary lookup. Member lookup only finds candidates of
    one class (a base's members come as shadows, which are in the derived
    class), so the rule matters only for spliced sets, whose candidates are
    the functions themselves.
- **Inside templates,** a non-dependent `^^f` is the set at the template's
  definition, as ordinary non-dependent lookup is. `^^T::f` is resolved at
  instantiation.
- **Hidden friends become callable by name:**
  `make_overload_set(hidden_friends_of(^^C))` gives a set, and splicing it
  calls the right one. Argument-dependent lookup is otherwise the only way to
  reach them.

## Library

```cpp
consteval auto is_overload_set(info r) -> bool;

// The candidates, in declaration order. A function or function template is
// its own only candidate.
consteval auto overloads_of(info r) -> vector<info>;

// The set of the given functions, function templates, and overload sets (whose
// candidates it includes). One candidate is reflected as itself; none is not a
// constant. If any candidate is a non-static member function, all must be
// members of one class or its bases: member call syntax needs a class for the
// object. Constructors and destructors are never candidates.
template <reflection_range R = initializer_list<info>>
consteval auto make_overload_set(R&& r) -> info;
```

Queries that a set's candidates agree on answer as one candidate would:

- `identifier_of` and `has_identifier`, when all candidates share a name. A set
  built from different names has no identifier, and operators have none
  either.
- `parent_of`, when all candidates share a scope.
- `source_location_of` gives the first candidate's location.

`type_of`, `constant_of`, `object_of`, `extract` and the other value and type
queries reject an overload set ("an overload set has no type"). `is_function`
and the other entity predicates are false.

## Not yet

- **Template arguments with linkage.** `use<^^f>()` needs a mangling: the
  candidates in a TU-independent order, with overloaded function templates
  distinguished by signature. The Itanium mangler currently gives "cannot yet
  mangle a reflection of an overload set as a template argument", without a
  source location. Constant evaluation and non-mangled uses work.
- **`reflect_invoke`/`substitute`** on a set, i.e. overload resolution through
  the library rather than a splice. This would be natural, and isn't done.
- **Serialization** (PCH/modules) writes a set as a null reflection, as for
  expression reflections.

## Implementation

- **Representation:** `ReflectionKind::OverloadSet` carries an
  `OverloadSetData` (`Reflection.h`). It is uniqued in a
  `FoldingSet<OverloadSetData>` in `ASTContext` (`ASTContext::getOverloadSet`),
  so equality and hashing are pointer comparisons.
- **The one constructor:** `makeOverloadSetReflection(ASTContext&, candidates)`
  (`APValue.cpp`) looks through shadows to the canonical declarations, removes
  duplicates, and sorts them into declaration order. Declarations at the same
  location, such as hidden friends instantiated from one pattern, are ordered
  by identity. It returns the single-candidate reflection or the null
  reflection where appropriate.
- **`^^`:** `BuildCXXReflectExpr` (`SemaReflect.cpp`) builds a set for
  `UnresolvedLookupExpr` (with no explicit template arguments),
  `UnresolvedMemberExpr`, multi-shadow `UsingDecl`, and `OverloadedTemplate`
  names.
- **Splice:** `BuildReflectionSpliceExpr` builds an `UnresolvedLookupExpr`
  over the candidates' most recent declarations (which have every default
  argument), with no ADL, no naming class (so no access check), the class as
  qualifier when all candidates are members of one class, and only templates
  when there are template arguments. Member access (`SemaExprMember.cpp`) adds
  every candidate to the `LookupResult`, after the "is the object's class
  derived from this member's class" check for each one. The acting-class rule
  is in `BuildCallToMemberFunction` (`SemaOverload.cpp`).
- **Metafunctions:** `is_overload_set` and `make_overload_set` (which reads a
  pointer and count, like `substitute`). `overloads_of` is kind 6 of
  `members_gather`.
- **Tests:** `overload-sets.pass.cpp` and `overload-sets.verify.cpp`.
