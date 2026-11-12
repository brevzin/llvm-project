// Demo of translate2.h: the full i18n pipeline (named message ids, plurals,
// validation) behind a *lazy* interface -- translate() returns a rebound
// pattern (std::rebound_format), so it plugs into the format ecosystem with no
// intermediate formatted string. See README.md.

#include "translate2.h"

#include <cassert>
#include <ostream>
#include <print>
#include <sstream>
#include <string>

// Same catalogs as examples/translate (in real life: loaded from files).
static void load_catalogs() {
  using namespace i18n;

  locales()["en"] = Locale{
      .messages =
          {
              {"Removed {n} files.",
               "{n, plural, one {Removed # file.} other {Removed # files.}}"},
          },
  };

  locales()["fr"] = Locale{
      .messages =
          {
              {"Hello, {name}!", "Bonjour, {name} !"},
              {"{name} commented on {post}.",
               "À propos de {post}, {name} a laissé un commentaire."},
              {"Removed {n} files.",
               "{n, plural, one {# fichier supprimé.} "
               "other {# fichiers supprimés.}}"},
              {"Hi, {user}!", "Salut, {user} !"},
              {"Total: {price:.2f}", "Total : {price:.2f} EUR"},
          },
      .plural = +[](long n) -> std::string_view {
        return n == 0 || n == 1 ? "one" : "other";
      },
  };

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
}

struct User {
  std::string display_name() const { return "Bob"; }
};

int main() {
  load_catalogs();
  assert(i18n::validate_catalogs().empty());

  std::string name = "Alice";
  std::string post = "P3951";

  // --- Lazy: the translated message formats straight to the output --------
  i18n::set_locale("fr");
  std::println(i18n2::translate(t"Hello, {name}!"));

  // --- ... or through any other consumer of the protocol ------------------
  auto msg = i18n2::translate(t"{name} commented on {post}.");

  // std::format
  assert(std::format(msg) == "À propos de P3951, Alice a laissé un commentaire.");

  // std::format_to into a buffer
  char buf[128];
  auto* end = std::format_to(buf, msg);
  assert(std::string_view(buf, end - buf) ==
         "À propos de P3951, Alice a laissé un commentaire.");

  // std::print to a stream; and the same object is reusable
  std::ostringstream sstr;
  std::print(sstr, msg);
  assert(sstr.str() == "À propos de P3951, Alice a laissé un commentaire.");

  // --- The whole pipeline still works through the lazy interface ----------

  // Plural selection happened at translate() time (it needs the value);
  // formatting is still deferred.
  i18n::set_locale("pl");
  auto removed = [](long n) {
    return std::format(i18n2::translate(t"Removed {n} files."));
  };
  assert(removed(1) == "Usunięto 1 plik.");
  assert(removed(2) == "Usunięto 2 pliki.");
  assert(removed(5) == "Usunięto 5 plików.");
  assert(removed(22) == "Usunięto 22 pliki.");
  assert(removed(25) == "Usunięto 25 plików.");

  // Name clause as translator-facing placeholder name.
  User u;
  i18n::set_locale("fr");
  assert(std::format(i18n2::translate(t"Hi, {u.display_name();"user"}!")) ==
         "Salut, Bob !");

  // Format specifiers survive translation.
  double price = 9.5;
  assert(std::format(i18n2::translate(t"Total: {price:.2f}")) ==
         "Total : 9.50 EUR");

  // Nested dynamic width, through the untranslated-fallback path.
  int width = 8;
  i18n::set_locale("tlh");
  assert(std::format(i18n2::translate(t"[{name:>{width}}]")) == "[   Alice]");

  std::println("All checks passed.");
}
