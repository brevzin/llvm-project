One of the things that we can't do with C++26 reflection is support overload sets. `^^f` is just ill-formed if `f` denotes an overload set. Let's change that! Now `^^f` is a reflection representing _that_ overload set.

At the point of formation, `^^f` captures the overload set at that point. Splicing a reflection representing an overload set will perform overload resolution, but _only_ amongst the candidates of the set. For instance:

```cpp
constexpr auto f(short ) -> int { return 1; } // #1
constexpr auto f(long ) -> int { return 2; }  // #2

constexpr auto r = ^^f; // ok, overload set of #1 and #2

constexpr auto f(int ) -> int { return 3; }  // #3

int i = [: r :](1);   // ill-formed: ambiguous
int j = [: ^^f :](1); // ok, calls #3
```

Overload sets can include any number of functions and function templates, as per usual.

An overload set can be decomposed into its constituent functions and function templates:

```cpp
// Constant When: r denotes an overload set
consteval auto overloads_of(info r) -> vector<info>;
```

More interestingly, an overload set can be arbitrarily combined too:

```cpp
// Constant When: every element of r is either a function or function template
template <reflection_range R = initializer_list<info>>
consteval auto make_overload_set(R&& r) -> info;
```

In which case we can perform overload resolution amongst candidates from different scopes from different names.

A "naturally" formed overload set will still have `identifier_of` return its identiifer, although an "artificial" one will only have an identifier if every element has the same one.
