// translate2.h - the synthesis of the two translation examples:
//
//  * examples/gettext-tstrings shows the *protocol*: translation returns
//    a rebound pattern (std::rebound_format), so it stays lazy and composes
//    entire format ecosystem (std::format / format_to / print / println).
//
//  * examples/translate shows the *pipeline*: compile-time message ids with
//    named placeholders, ICU-style plural selection, catalog validation,
//    and extraction-from-compilation.
//
// This header layers the first on top of the second. The key observation
// making that feasible: plural selection and placeholder renumbering need
// the interpolated *values*, but the values are already bound when
// translate() is called -- so all pattern rewriting happens eagerly at
// translate time, while the (potentially much more expensive) formatting
// stays deferred. The wrapper owns the rewritten pattern and forwards
// exprs() to the original template string.

#ifndef I18N_TRANSLATE2_H
#define I18N_TRANSLATE2_H

#include "../translate1/translate.h"

namespace i18n2 {

template <std::template_string S>
auto translate(S ts) {
  static_assert(i18n::names_are_identifier_like<S>(),
                "translatable placeholder names must be identifier-like; "
                "name the field with a name clause: {expr;\"name\"}");
  (void)&i18n::registered<S>; // compiling this message registers it

  i18n::MessageDesc desc{i18n::msgid<S>, i18n::Meta<S>::names,
                         i18n::Meta<S>::first_arg, i18n::Meta<S>::arg_count};

  std::string_view pattern = desc.id;
  i18n::Locale const* loc = nullptr;
  if (auto it = i18n::locales().find(i18n::current_locale());
      it != i18n::locales().end()) {
    loc = &it->second;
    if (auto m = loc->messages.find(desc.id); m != loc->messages.end())
      pattern = m->second;
  }
  i18n::PluralRule rule = loc ? loc->plural : i18n::Locale{}.plural;

  auto const& [...parts] = ts.exprs();
  auto get_int = [&](std::size_t idx) -> long {
    long result = 0;
    bool found = false;
    std::size_t i = 0;
    ([&] {
      if (i++ == idx) {
        if constexpr (std::integral<std::remove_cvref_t<decltype(parts)>>) {
          result = static_cast<long>(parts);
          found = true;
        }
      }
    }(),
     ...);
    if (!found)
      throw std::runtime_error("plural selector is not an integer");
    return result;
  };

  std::string indexed =
      i18n::PatternRenderer(desc, rule, get_int).render(pattern);

  // std::rebind_format pairs the owned, renumbered pattern with the
  // original template string's arguments; the result (a rebound_format) is
  // what the format ecosystem consumes, so formatting stays deferred.
  return std::rebind_format(std::move(indexed), std::move(ts));
}

} // namespace i18n2

#endif // I18N_TRANSLATE2_H
