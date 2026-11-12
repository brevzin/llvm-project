// Internationalization with C++26 template strings (P3951) -- live demo.
//
// The .po files in this project are the external translation catalogs;
// edit one (say, a Polish plural variant) and recompile to see the output
// change. The first section of output is the .pot translation template,
// which the binary extracts from its own i18n_messages ELF section --
// including a message from a function that is never called.

#include "translate3.h"

#include <ostream>
#include <print>
#include <string>

static constexpr char en_po[] = {
#embed "en.po"
, '\0'};
static constexpr char fr_po[] = {
#embed "fr.po"
, '\0'};
static constexpr char pl_po[] = {
#embed "pl.po"
, '\0'};
static constexpr char de_po[] = {
#embed "de.po"
, '\0'};

struct User {
  std::string display_name() const { return "Bob"; }
};

// Never runs -- but it is compiled, so its message is extracted anyway.
[[maybe_unused]] static std::string never_called() {
  int retries = 3;
  return std::format(
      i18n::translate(t"Connection lost, retrying ({retries} attempts left)..."));
}

int main() {
  i18n::load_po("en", en_po);
  i18n::load_po("fr", fr_po);
  i18n::load_po("pl", pl_po);
  i18n::load_po("de", de_po);

  // --- The .pot template, extracted from this binary's own ELF section ----
  std::println("=== extracted .pot (from the binary itself) ===");
  std::print("{}", i18n::extracted_pot());

  // --- Catalogs validated against the binary's message metadata -----------
  std::println("=== catalog validation ===");
  for (auto const& e : i18n::validate_catalogs())
    std::println("error: {}", e); // de.po has a deliberate {naem} typo

  // --- Translation: lookup by message id, reordering, laziness ------------
  std::println("\n=== translation ===");
  std::string name = "Alice";
  std::string post = "P3951";
  for (auto loc : {"en", "fr"}) {
    i18n::set_locale(loc);
    std::print("[{}] ", loc);
    std::println(i18n::translate(t"Hello, {name}!"));
    std::print("[{}] ", loc);
    std::println(i18n::translate(t"{name} commented on {post}."));
  }

  // --- Plurals, driven by each catalog's own Plural-Forms formula ---------
  std::println("\n=== plurals ===");
  for (auto loc : {"en", "fr", "pl"}) {
    i18n::set_locale(loc);
    std::print("[{}]", loc);
    for (long n : {0L, 1L, 2L, 5L, 22L})
      std::print("  {}", i18n::translate(t"Removed {n} files."));
    std::println("");
  }

  // --- The name clause names the placeholder for translators --------------
  std::println("\n=== name clause, specifiers ===");
  User u;
  double price = 9.5;
  i18n::set_locale("fr");
  std::println(i18n::translate(t"Hi, {u.display_name();"user"}!"));
  std::println(i18n::translate(t"Total: {price:.2f}"));
}
