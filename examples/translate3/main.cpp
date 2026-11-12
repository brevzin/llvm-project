// Demo of translate3.h: the external-catalog workflow. Translations live in
// PO files on disk (locale/*.po) that translators edit; the program loads
// them at runtime; the .pot template is extracted from the binary itself
// (`make extract`). See README.md.

#include "translate3.h"

#include <cassert>
#include <ostream>
#include <print>
#include <sstream>
#include <string>

struct User {
  std::string display_name() const { return "Bob"; }
};

// A message in code that never runs is still compiled, so it still lands in
// the binary's i18n_messages section: `make extract` includes it.
[[maybe_unused]] static std::string never_called() {
  int retries = 3;
  return std::format(
      i18n::translate(t"Connection lost, retrying ({retries} attempts left)..."));
}

int main() {
  // --- Load external catalogs ---------------------------------------------
  bool fr = i18n::load_po("fr", "locale/fr.po");
  bool pl = i18n::load_po("pl", "locale/pl.po");
  bool de = i18n::load_po("de", "locale/de.po");
  assert(fr && pl && de);
  std::println("loaded {} catalogs", i18n::locales().size());

  // --- Validate them against the binary's own message metadata ------------
  auto errors = i18n::validate_catalogs();
  for (auto const& e : errors)
    std::println("validation error: {}", e);
  assert(errors.size() == 1);
  assert(errors[0].find("naem") != std::string::npos);

  std::string name = "Alice";
  std::string post = "P3951";

  // --- Untranslated locale: message id is the fallback pattern ------------
  i18n::set_locale("en");
  assert(std::format(i18n::translate(t"Hello, {name}!")) == "Hello, Alice!");

  // --- Lazy: translated messages flow into the format ecosystem -----------
  i18n::set_locale("fr");
  std::println(i18n::translate(t"Hello, {name}!"));

  auto msg = i18n::translate(t"{name} commented on {post}.");
  assert(std::format(msg) ==
         "À propos de P3951, Alice a laissé un commentaire.");
  char buf[128];
  auto* end = std::format_to(buf, msg);
  assert(std::string_view(buf, end - buf) ==
         "À propos de P3951, Alice a laissé un commentaire.");
  std::ostringstream sstr;
  std::print(sstr, msg);
  assert(sstr.str() == "À propos de P3951, Alice a laissé un commentaire.");

  // --- Plurals, selected by each catalog's own Plural-Forms formula -------
  auto removed = [](long n) {
    return std::format(i18n::translate(t"Removed {n} files."));
  };

  i18n::set_locale("fr"); // nplurals=2; plural=n>1  (0 and 1 are singular)
  assert(removed(0) == "0 fichier supprimé.");
  assert(removed(1) == "1 fichier supprimé.");
  assert(removed(5) == "5 fichiers supprimés.");

  i18n::set_locale("pl"); // the real three-way Polish formula
  assert(removed(1) == "Usunięto 1 plik.");
  assert(removed(2) == "Usunięto 2 pliki.");
  assert(removed(5) == "Usunięto 5 plików.");
  assert(removed(22) == "Usunięto 22 pliki.");
  assert(removed(25) == "Usunięto 25 plików.");

  std::print("pl:");
  for (long n : {1L, 2L, 5L, 22L, 25L})
    std::print("  [{}]", removed(n));
  std::println("");

  // --- Name clause as the translator-facing placeholder name --------------
  User u;
  i18n::set_locale("fr");
  assert(std::format(i18n::translate(t"Hi, {u.display_name();"user"}!")) ==
         "Salut, Bob !");

  // --- Format specifiers survive translation ------------------------------
  double price = 9.5;
  assert(std::format(i18n::translate(t"Total: {price:.2f}")) ==
         "Total : 9.50 EUR");

  // --- Nested dynamic width through the fallback path ---------------------
  int width = 8;
  i18n::set_locale("en");
  assert(std::format(i18n::translate(t"[{name:>{width}}]")) == "[   Alice]");

  std::println("All checks passed.");
}
