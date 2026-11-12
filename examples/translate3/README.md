# External Translation Catalogs with Template Strings

Self-contained example demonstrating the *external* workflow that real
internationalization pipelines use, built on C++26 template strings (P3951).
Where `examples/translate1` and `examples/translate2` use hardcoded in-memory
catalogs, this one runs the actual three-phase gettext-style pipeline:

## 1. Extraction — from the binary, not from grep

Every `translate()`d message deposits a `.pot` record (message id,
placeholder names, *and their C++ types*) into a dedicated ELF section at
compile time, via a `consteval`-built char array marked
`[[gnu::used, gnu::section("i18n_messages")]]`. Extraction is then:

```sh
make extract
# objcopy -O binary --only-section=i18n_messages main messages.bin
# tr -s '\0' '\n' < messages.bin | awk <dedup>  > messages.pot
```

producing entries like:

```
#. placeholders: price: double
msgid "Total: {price:.2f}"
msgstr ""
```

Compare `xgettext`, which regex-scans source: this extraction requires no
execution, sees through every macro, includes messages in code that never
runs (the demo has one), and cannot drift from the binary actually shipped.
The embedded argument types are what would let a vendor-side checker verify
translations against the real message signatures. (Two identical literals
produce two records — distinct closure-like types — hence the `msguniq`-style
dedup in the pipeline, exactly as in real PO workflows.)

## 2. Translation — PO files on disk, with real Plural-Forms

Translators edit `locale/*.po`; the program never contains translations.
The loader handles PO syntax (msgid/msgstr, continuation lines, comments,
the header entry) and — the authentic gettext bit — evaluates the header's
`Plural-Forms` expression with a small interpreter:

```
"Plural-Forms: nplurals=3; plural=n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<12 || n%100>14) ? 1 : 2;\n"
```

Demo dialect: plural variants live inside the msgstr pattern, keyed by the
formula's indices (rather than gettext's parallel `msgstr[n]` entries):

```
msgid "Removed {n} files."
msgstr "{n, plural, 0 {Usunięto # plik.} 1 {Usunięto # pliki.} 2 {Usunięto # plików.}}"
```

Translations may reorder, repeat, or drop the named placeholders; `#`
formats the plural selector's value.

## 3. Load + format — validated, lazy

`load_po("pl", "locale/pl.po")` at startup; `validate_catalogs()` checks
every translation of every compiled message against that message's static
placeholder metadata (the demo ships a broken `de.po` that it catches).
`translate()` is lazy as in `examples/translate2`: it returns a
`std::rebound_format`, so translated messages flow into `std::format`,
`format_to`, `print`, and `println` with no intermediate formatted string —
pattern lookup, plural selection, and renumbering happen at translate time
(they need the bound values); formatting stays deferred.

## Building

```sh
make CXX=path/to/build/bin/clang++ run
make CXX=path/to/build/bin/clang++ extract
```

## Demo limitations

- The PO dialect: no `msgctxt`, no `msgid_plural`/`msgstr[n]` (plural
  variants are inline, ICU-style), minimal escape handling.
- Extraction relies on ELF + objcopy (`[[gnu::section]]`); other object
  formats would need their own spelling of the same idea.
- Locale fallback chains (fr_CA -> fr -> en), translator comments flowing
  into the section records, and locale-aware value formatting (`{:L}`) are
  all straightforward extensions -- none need anything from the language.
