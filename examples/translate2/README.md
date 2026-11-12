# Lazy Translation with Template Strings

The synthesis of the two sibling examples:

- `examples/gettext-tstrings` demonstrates the **interface**: translation
  returns a deferred formatting bundle, so it composes with the whole
  format ecosystem without eagerly formatting.
- `examples/translate1` demonstrates the **pipeline**: compile-time message
  ids with named placeholders, ICU-style plurals, catalog validation, and
  extraction-from-compilation — but returns an eagerly formatted
  `std::string`.

This example layers the first on top of the second (`translate2.h` includes
and reuses `../translate1/translate.h` wholesale — only the last step
differs):

```cpp
i18n::set_locale("fr");
std::println(i18n2::translate(t"Hello, {name}!"));   // no intermediate string

auto msg = i18n2::translate(t"{name} commented on {post}.");
std::format(msg);            // reusable value,
std::format_to(buf, msg);    // works with every consumer
std::print(stream, msg);     // of the protocol
```

## The feasibility question this answers

Plural selection and named-to-indexed placeholder renumbering need the
interpolated *values*, which seems to conflict with laziness. It doesn't:
the values are already bound when `translate()` is called, so all pattern
rewriting happens eagerly at translate time, while formatting stays
deferred. The wrapper owns the rewritten pattern (a `std::string` — the
renumbered pattern exists nowhere else to point at, which is the one cost
relative to the `gettext-tstrings` wrapper's `string_view` into the
catalog), exposes it via `fmt()` as a `std::dynamic_format` string, and
forwards `exprs()` to the original template string.

Two consequences of "rewriting happens at translate() time" worth knowing:

- The result is pinned to the locale (and plural category) in effect when
  `translate()` ran; changing the locale afterwards does not retranslate an
  existing wrapper.
- Like any template string, the wrapper holds its interpolands by
  reference, so it is meant to be consumed while they are alive — pass it
  along, don't store it.

## Building

```sh
make CXX=path/to/build/bin/clang++ run
```
