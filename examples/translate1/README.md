# Internationalization with Template Strings

A proof-of-concept gettext/ICU-style translation library built on C++26
template strings (P3951). Where the sibling `gettext-tstrings` example shows
the basic mechanism (use `fmt()` as the message id, feed a translated pattern
back through `std::format`), this one demonstrates the parts of a real i18n
pipeline that only work because the template string carries *named, typed,
consteval-accessible* structure.

Every eagerly-interpolated string design shipped so far is documented as
unsuitable for localization (C# `$"..."`, Python f-strings): interpolation
destroys the pattern before translation can swap it, and fuses the argument
order. Deferred interpolation is the fix, and template strings carry more
static information than any of the prior art (C# `FormattableString`, JS
tagged templates, Python PEP 750 t-strings).

## What the demo shows

```cpp
std::string name = "Alice";
i18n::set_locale("fr");
i18n::translate(t"Hello, {name}!");   // "Bonjour, Alice !"
```

1. **Compile-time message ids with named placeholders.** The id of
   `t"Hello, {name}!"` is `"Hello, {name}!"`, synthesized by a `consteval`
   function from `S::string(i)` + `S::interpolation(i).expression`. Names
   beat gettext's `%s`/`%1$s` and `std::format`'s `{0}` for translators.

2. **Reordering (and repetition).** A translation is a pattern over the same
   names; the renderer rewrites it to `std::format` arg-ids at runtime:
   `"{name} commented on {post}."` becomes
   `"À propos de {post}, {name} a laissé un commentaire."`.

3. **ICU-style plural selection** with real per-locale plural rules,
   including Polish's three categories:
   `"{n, plural, one {Usunięto # plik.} few {Usunięto # pliki.} many {Usunięto # plików.}}"`.
   The library can do this because it holds the typed values: the selector's
   integer is fetched out of the interpolation pack at runtime.

4. **The name clause is the translator-facing name.** P3951's
   `{expr;"name"}` clause (which exists so spelling-derived data survives
   preprocessing) doubles as ICU/Lingui-style explicit placeholder naming:

   ```cpp
   i18n::translate(t"Hi, {u.display_name();"user"}!");   // id: "Hi, {user}!"
   ```

   `translate()` statically rejects placeholder names that aren't
   identifier-like, so `t"Value: {x + 1}"` fails to compile with a message
   telling you to add a clause.

5. **Extraction from compilation, not from grep.** Each `translate()`
   instantiation registers its message; `main` dumps a `.pot` analogue that
   includes a message from a function that is never called. Unlike
   `xgettext`, this sees through macros and can never drift from the code.

6. **Catalog validation against static metadata.** Every translation of
   every compiled message is checked (placeholder names, plural syntax)
   before anything is formatted — the demo ships a deliberately broken
   German entry (`{naem}`) that validation catches. This is `msgfmt --check`
   but driven by the program's own compile-time knowledge of each message's
   arguments; a real implementation could also check formattability per
   argument type.

7. **Format specifiers survive**, including nested dynamic widths:
   `t"[{name:>{width}}]"` has id `"[{name:>{}}]"` and renders through the
   same renumbering machinery.

## Building

```sh
make CXX=path/to/build/bin/clang++ run
```

(Defaults to `../../build/bin/clang++`; needs the p2996 branch's clang and
libc++ with template string support and `-freflection`.)

## Limitations (it's a demo)

- The plural grammar is ICU-lite: `{name, plural, kw {...} ...}` only, no
  `select`/`selectordinal`, no `offset:`, and no `{{`/`}}` escapes inside
  variants. Nested plurals do work (the renderer recurses).
- Catalogs are in-memory maps; a real library would load `.po`-style files
  and expose the extracted metadata to catalog tooling.
- Messages inside templates that are never instantiated never register;
  a production extractor would pair this with source scanning.
