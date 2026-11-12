# Translation with Template Strings (t-strings)

This example demonstrates how C++26 template strings (t-strings) enable clean integration with gettext-style translation systems.

## The Problem with Traditional Approaches

Traditional C++ translation approaches have tradeoffs:

```cpp
// Option 1: printf-style (error-prone, no type safety)
printf(gettext("Hello, %s!"), name);

// Option 2: Positional arguments (verbose, still error-prone)
fmt::format(fmt::runtime(gettext("Hello, {0}!")), name);

// Option 3: Concatenation (breaks translation context)
std::cout << gettext("Hello, ") << name << gettext("!");
```

## The T-String Solution

Template strings separate the format string from the interpolated values:

```cpp
auto msg = t"Hello, {name}!";
// msg.fmt() returns "Hello, {}!" - the translatable message ID
// The values (name) are captured separately
```

This enables:

1. **Type-safe interpolation** - values are captured with their types
2. **Clean message IDs** - `msg.fmt()` gives a simple format string for translation
3. **Runtime format selection** - translate the format, apply it to the captured values

## How It Works

```cpp
template <std::template_string TString>
std::string translate(TString&& ts, Language lang) {
    // Get the translated format string
    auto translated_fmt = gettext(ts.fmt(), lang);
    
    // Extract all interpolated values using pack expansion
    auto& [...parts] = ts;
    
    // Apply translated format to the original values
    return std::vformat(translated_fmt, std::make_format_args(parts...));
}

// Usage
std::string_view name = "Alice";
std::cout << translate(t"Hello, {name}!", Language::German);
// Output: "Hallo, Alice!"
```

## Building

Requires a C++26 compiler with reflection and expansion statements support.

From the llvm-project build directory:

```bash
./bin/clang++ -std=c++26 -stdlib=libc++ -freflection -fexpansion-statements \
    -Wl,-rpath,$(pwd)/lib/x86_64-unknown-linux-gnu \
    -o gettext-demo ../examples/gettext-tstrings/main.cpp
./gettext-demo
```

## Argument Reordering

Different languages have different word orders. Translations can use positional
arguments (`{0}`, `{1}`, `{2}`) to reorder the interpolated values:

```cpp
// Source code (English word order)
t"Copied {count} files from {source} to {dest}."
// fmt() returns: "Copied {} files from {} to {}."

// Japanese translation reorders arguments (source, dest, count):
"{1}から{2}へ{0}個のファイルをコピーしました。"
```

Output:
```
English: Copied 42 files from /home/data to /backup.
Japanese: /home/dataから/backupへ42個のファイルをコピーしました。
```

The values are captured in source order but can be referenced by position in
the translated format string.

## Dropping Arguments

Translations can also omit arguments entirely. This is useful when certain
details aren't needed or are implicit in the target language:

```cpp
// Source with 3 arguments (code, file, line)
t"Error {code}: Not found (file: {file}, line: {line})."
// fmt() returns: "Error {}: Not found (file: {}, line: {})."

// German keeps all args, translates the message:
"Fehler {0}: Nicht gefunden (Datei: {1}, Zeile: {2})."

// French drops file and line:
"Erreur {0} : Non trouvé."

// Japanese also simplifies:
"エラー{0}: 見つかりません"
```

Output:
```
English:  Error 404: Not found (file: main.cpp, line: 42).
German:   Fehler 404: Nicht gefunden (Datei: main.cpp, Zeile: 42).
French:   Erreur 404 : Non trouvé.
Japanese: エラー404: 見つかりません
```

## Example Output

```
=== English ===
Hello, Alice!
Copied 42 files from /home/data to /backup.
Dear Alice, your order #12345 will arrive on March 15.

=== German ===
Hallo, Alice!
42 Dateien wurden von /home/data nach /backup kopiert.
Sehr geehrte(r) Alice, Ihre Bestellung #12345 wird am 15. März eintreffen.

=== Japanese ===
こんにちは、Aliceさん！
/home/dataから/backupへ42個のファイルをコピーしました。
Alice様、ご注文番号12345は3月15日に届く予定です。
```

## Integration with Real gettext

For production use, replace the mock `gettext()` function with the real GNU gettext:

```cpp
#include <libintl.h>

std::string_view gettext_wrapper(std::string_view msgid) {
    return ::gettext(std::string(msgid).c_str());
}
```

The message catalog (.po file) would contain entries like:

```po
msgid "Hello, {}!"
msgstr "Hallo, {}!"

msgid "You have {} new messages."
msgstr "Du hast {} neue Nachrichten."
```
