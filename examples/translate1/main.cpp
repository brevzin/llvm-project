// Demo of translate.h: gettext/ICU-style internationalization on top of
// C++26 template strings. See README.md.

#include "translate.h"

#include <cassert>
#include <print>
#include <string>

// ---------------------------------------------------------------------------
// Catalogs (in real life: loaded from .po/.mo-style files)
// ---------------------------------------------------------------------------

static void load_catalogs() {
  using namespace i18n;

  // Even the source language can use plural selection: the message id is
  // just the fallback pattern.
  locales()["en"] = Locale{
      .messages =
          {
              {"Removed {n} files.",
               "{n, plural, one {Removed # file.} other {Removed # files.}}"},
          },
      .plural = +[](long n) -> std::string_view {
        return n == 1 ? "one" : "other";
      },
  };

  locales()["fr"] = Locale{
      .messages =
          {
              {"Hello, {name}!", "Bonjour, {name} !"},
              {"Goodbye!", "Au revoir !"},
              // Translations may reorder (and even repeat) placeholders.
              {"{name} commented on {post}.",
               "À propos de {post}, {name} a laissé un commentaire."},
              {"Removed {n} files.",
               "{n, plural, one {# fichier supprimé.} "
               "other {# fichiers supprimés.}}"},
              // The name clause gave this field a translator-friendly name.
              {"Hi, {user}!", "Salut, {user} !"},
              // Format specifiers pass through (and could be adjusted).
              {"Total: {price:.2f}", "Total : {price:.2f} EUR"},
          },
      .plural = +[](long n) -> std::string_view {
        return n == 0 || n == 1 ? "one" : "other";
      },
  };

  // Polish: three plural categories, the classic hard case.
  locales()["pl"] = Locale{
      .messages =
          {
              {"Removed {n} files.",
               "{n, plural, one {Usunięto # plik.} few {Usunięto # pliki.} "
               "many {Usunięto # plików.}}"},
          },
      .plural = +[](long n) -> std::string_view {
        if (n == 1)
          return "one";
        if (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14))
          return "few";
        return "many";
      },
  };

  // Deliberately broken, to demonstrate catalog validation.
  locales()["de"] = Locale{
      .messages =
          {
              {"Hello, {name}!", "Hallo, {naem}!"},
          },
  };
}

// ---------------------------------------------------------------------------
// A message in code that never runs still gets compiled, registered, and
// therefore extracted.
// ---------------------------------------------------------------------------

[[maybe_unused]] static std::string never_called() {
  int retries = 3;
  return i18n::translate(t"Connection lost, retrying ({retries} attempts left)...");
}

struct User {
  std::string display_name() const { return "Bob"; }
};

int main() {
  load_catalogs();

  // --- Extraction: dump the .pot analogue from the registry --------------
  std::println("=== extracted message template (.pot analogue) ===");
  std::vector<std::string_view> seen;
  for (auto const& msg : i18n::all_messages()) {
    if (std::find(seen.begin(), seen.end(), msg.id) != seen.end())
      continue;
    seen.push_back(msg.id);
    std::print("msgid \"{}\"", msg.id);
    if (!msg.names.empty()) {
      std::print("   # placeholders:");
      for (auto n : msg.names)
        std::print(" {{{}}}", n);
    }
    std::println("");
  }

  // --- Validation: catch translator errors before they are formatted -----
  std::println("\n=== catalog validation ===");
  auto errors = i18n::validate_catalogs();
  for (auto const& e : errors)
    std::println("error: {}", e);
  assert(errors.size() == 1);
  assert(errors[0].find("naem") != std::string::npos);

  // --- Basic lookup, fallback, reordering ---------------------------------
  std::string name = "Alice";
  std::string post = "P3951";

  i18n::set_locale("en");
  assert(i18n::translate(t"Hello, {name}!") == "Hello, Alice!");
  assert(i18n::translate(t"{name} commented on {post}.") ==
         "Alice commented on P3951.");

  i18n::set_locale("fr");
  assert(i18n::translate(t"Hello, {name}!") == "Bonjour, Alice !");
  assert(i18n::translate(t"Goodbye!") == "Au revoir !");
  // Reordered: {post} now comes first.
  assert(i18n::translate(t"{name} commented on {post}.") ==
         "À propos de P3951, Alice a laissé un commentaire.");

  // --- Plurals -------------------------------------------------------------
  std::println("\n=== plurals ===");
  for (auto loc : {"en", "fr", "pl"}) {
    i18n::set_locale(loc);
    std::print("{}:", loc);
    for (long n : {0L, 1L, 2L, 5L, 22L, 25L}) {
      std::print("  [{}]", i18n::translate(t"Removed {n} files."));
    }
    std::println("");
  }
  // An expression that is not a usable placeholder name must be named with
  // the name clause -- enforced at compile time (translate() static_asserts
  // otherwise). Note this message has the *same id* as the {n} one above, so
  // they share a catalog entry.
  i18n::set_locale("en");
  assert(i18n::translate(t"Removed {[]{ return 1; }();"n"} files.") ==
         "Removed 1 file.");
  {
    long n = 5;
    assert(i18n::translate(t"Removed {n} files.") == "Removed 5 files.");
  }
  i18n::set_locale("pl");
  {
    auto removed = [](long n) { return i18n::translate(t"Removed {n} files."); };
    assert(removed(1) == "Usunięto 1 plik.");
    assert(removed(2) == "Usunięto 2 pliki.");
    assert(removed(5) == "Usunięto 5 plików.");
    assert(removed(22) == "Usunięto 22 pliki.");
    assert(removed(25) == "Usunięto 25 plików.");
  }

  // --- The name clause names the placeholder for translators --------------
  User u;
  i18n::set_locale("en");
  assert(i18n::translate(t"Hi, {u.display_name();"user"}!") == "Hi, Bob!");
  i18n::set_locale("fr");
  assert(i18n::translate(t"Hi, {u.display_name();"user"}!") == "Salut, Bob !");

  // --- Format specifiers survive translation ------------------------------
  double price = 9.5;
  i18n::set_locale("en");
  assert(i18n::translate(t"Total: {price:.2f}") == "Total: 9.50");
  i18n::set_locale("fr");
  assert(i18n::translate(t"Total: {price:.2f}") == "Total : 9.50 EUR");

  // --- Nested fields (dynamic width) render through the same machinery ----
  int width = 8;
  i18n::set_locale("en");
  assert(i18n::translate(t"[{name:>{width}}]") == "[   Alice]");

  // --- Untranslated locale falls back to the message id -------------------
  i18n::set_locale("tlh"); // no Klingon catalog
  assert(i18n::translate(t"Hello, {name}!") == "Hello, Alice!");

  std::println("\nAll checks passed.");
}
