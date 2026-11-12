// translate.h - a proof-of-concept internationalization library built on
// C++26 template strings (P3951).
//
// The pipeline mirrors gettext/ICU MessageFormat:
//
//  * The message id is synthesized AT COMPILE TIME from the template string's
//    pieces and expression texts: t"Hello, {name}!" has the message id
//    "Hello, {name}!". Placeholders are *named* (like ICU, unlike printf),
//    and the name is the interpolated expression -- or the name clause:
//    t"Hi, {u.display_name();"user"}!" has the id "Hi, {user}!".
//
//  * Translations are patterns over the same names, free to reorder, repeat,
//    or drop placeholders, and to use ICU-style plural selection:
//      "{n, plural, one {# file} other {# files}}"
//    with a per-locale plural rule choosing the variant and '#' formatting
//    the selector's value.
//
//  * Every message that is *compiled* registers itself (even in functions
//    that never run), so the full catalog template (.pot analogue) can be
//    dumped from the program, and every translation can be validated against
//    the message's placeholder names before it is ever formatted.

#ifndef I18N_TRANSLATE_H
#define I18N_TRANSLATE_H

#include <array>
#include <cstddef>
#include <format>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace i18n {

// ---------------------------------------------------------------------------
// Compile-time message metadata
// ---------------------------------------------------------------------------

consteval std::size_t cstrlen_(char const* s) {
  std::size_t n = 0;
  while (s[n])
    ++n;
  return n;
}

// The message id is the string pieces with each replacement field written as
// {name} followed by its original format specifier: t"Total: {price:.2f}"
// has the id "Total: {price:.2f}", so untranslated messages format exactly
// as written and translators see (and may adjust) the specifier.
template <std::template_string S>
consteval std::size_t msgid_size() {
  std::size_t n = 0;
  for (std::size_t i = 0; i <= S::num_interpolations(); ++i) {
    n += cstrlen_(S::string(i));
    if (i < S::num_interpolations()) {
      n += 1 + cstrlen_(S::interpolation(i).expression);   // {name
      n += cstrlen_(S::interpolation(i).fmt) - 1;          // :spec} or }
    }
  }
  return n;
}

template <std::template_string S>
consteval auto build_msgid() {
  std::array<char, msgid_size<S>() + 1> buf{};
  std::size_t o = 0;
  auto put = [&](char const* s) {
    while (*s)
      buf[o++] = *s++;
  };
  for (std::size_t i = 0; i <= S::num_interpolations(); ++i) {
    put(S::string(i));
    if (i < S::num_interpolations()) {
      buf[o++] = '{';
      put(S::interpolation(i).expression);
      put(S::interpolation(i).fmt + 1); // "{:spec}" minus its '{'
    }
  }
  return buf;
}

// Placeholder names appear in catalogs, so they must not collide with the
// pattern syntax. An expression like {compute() * 2} is not a usable name:
// give the field one with the name clause, {compute() * 2;"total"}.
template <std::template_string S>
consteval bool names_are_identifier_like() {
  for (std::size_t i = 0; i < S::num_interpolations(); ++i) {
    char const* name = S::interpolation(i).expression;
    if (!*name)
      return false;
    for (char const* p = name; *p; ++p) {
      char c = *p;
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_'))
        return false;
    }
  }
  return true;
}

template <std::template_string S>
inline constexpr auto msgid_storage = build_msgid<S>();

template <std::template_string S>
inline constexpr std::string_view msgid{msgid_storage<S>.data(),
                                        msgid_storage<S>.size() - 1};

template <std::template_string S>
struct Meta {
  static constexpr std::size_t N = S::num_interpolations();

  static constexpr auto names = [] {
    std::array<std::string_view, N> a;
    for (std::size_t i = 0; i < N; ++i)
      a[i] = S::interpolation(i).expression;
    return a;
  }();

  // First format argument of each field and how many it uses (1 + any
  // expressions nested in its specifier, e.g. a dynamic width).
  static constexpr auto first_arg = [] {
    std::array<std::size_t, N> a;
    for (std::size_t i = 0; i < N; ++i)
      a[i] = S::interpolation(i).index;
    return a;
  }();
  static constexpr auto arg_count = [] {
    std::array<std::size_t, N> a;
    for (std::size_t i = 0; i < N; ++i)
      a[i] = S::interpolation(i).count;
    return a;
  }();
};

// ---------------------------------------------------------------------------
// Message registry: one entry per message *compiled into* the program
// ---------------------------------------------------------------------------

struct MessageDesc {
  std::string_view id;
  std::span<std::string_view const> names;
  std::span<std::size_t const> first_arg;
  std::span<std::size_t const> arg_count;
};

inline std::vector<MessageDesc>& all_messages() {
  static std::vector<MessageDesc> msgs;
  return msgs;
}

template <std::template_string S>
struct Register {
  Register() {
    all_messages().push_back({msgid<S>, Meta<S>::names, Meta<S>::first_arg,
                              Meta<S>::arg_count});
  }
};
template <std::template_string S>
inline Register<S> registered{};

// ---------------------------------------------------------------------------
// Locales and catalogs
// ---------------------------------------------------------------------------

using PluralRule = std::string_view (*)(long);

struct Locale {
  std::map<std::string_view, std::string_view> messages;
  PluralRule plural = +[](long n) -> std::string_view {
    return n == 1 ? "one" : "other";
  };
};

inline std::map<std::string_view, Locale>& locales() {
  static std::map<std::string_view, Locale> m;
  return m;
}

inline std::string_view& current_locale() {
  static std::string_view loc = "en";
  return loc;
}

inline void set_locale(std::string_view loc) { current_locale() = loc; }

// ---------------------------------------------------------------------------
// Pattern renderer: rewrites a translated pattern with named placeholders
// into a std::format pattern with numeric arg-ids (which is what makes
// reordering work), selecting plural variants along the way.
// ---------------------------------------------------------------------------

class PatternRenderer {
  static constexpr std::size_t npos = std::size_t(-1);

  MessageDesc const& Msg;
  PluralRule Plural;
  // Returns the value of format argument #i, for plural selection. Null in
  // validation mode, where every variant is rendered instead.
  std::function<long(std::size_t)> GetInt;
  std::string Out;

  [[noreturn]] void fail(std::string const& why) const {
    throw std::runtime_error("message \"" + std::string(Msg.id) + "\": " + why);
  }

  std::size_t field(std::string_view name) const {
    for (std::size_t i = 0; i < Msg.names.size(); ++i)
      if (Msg.names[i] == name)
        return i;
    fail("no placeholder named '" + std::string(name) + "'");
  }

  static std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ')
      s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ')
      s.remove_suffix(1);
    return s;
  }

  // Renders one chunk of pattern text. PluralArg is the argument whose value
  // '#' formats, when inside a plural variant.
  void chunk(std::string_view pat, std::size_t plural_arg) {
    for (std::size_t i = 0; i < pat.size(); ++i) {
      char c = pat[i];
      if (c == '#' && plural_arg != npos) {
        Out += '{';
        Out += std::to_string(plural_arg);
        Out += '}';
        continue;
      }
      if (c == '}') {
        if (i + 1 < pat.size() && pat[i + 1] == '}') {
          Out += "}}";
          ++i;
          continue;
        }
        fail("unbalanced '}'");
      }
      if (c != '{') {
        Out += c;
        continue;
      }
      if (i + 1 < pat.size() && pat[i + 1] == '{') {
        Out += "{{";
        ++i;
        continue;
      }

      // A replacement field: {name}, {name:spec} or {name, plural, ...}.
      std::size_t start = ++i;
      while (i < pat.size() && pat[i] != ':' && pat[i] != ',' && pat[i] != '}')
        ++i;
      if (i == pat.size())
        fail("unterminated replacement field");
      std::size_t k = field(trim(pat.substr(start, i - start)));

      if (pat[i] == '}') {
        Out += '{';
        Out += std::to_string(Msg.first_arg[k]);
        Out += '}';
      } else if (pat[i] == ':') {
        spec(pat, i, k);
      } else {
        plural(pat, i, k);
      }
    }
  }

  // Copies a format specifier, renumbering any nested "{}" to the field's
  // extra arguments (e.g. a dynamic width). On entry pat[i] == ':'.
  void spec(std::string_view pat, std::size_t& i, std::size_t k) {
    Out += '{';
    Out += std::to_string(Msg.first_arg[k]);
    std::size_t next_nested = 1;
    unsigned depth = 1;
    for (; i < pat.size(); ++i) {
      char c = pat[i];
      if (c == '{' && i + 1 < pat.size() && pat[i + 1] == '}') {
        if (next_nested >= Msg.arg_count[k])
          fail("more nested fields in specifier than the message provides");
        Out += '{';
        Out += std::to_string(Msg.first_arg[k] + next_nested++);
        Out += '}';
        ++i;
        continue;
      }
      if (c == '{')
        ++depth;
      else if (c == '}' && --depth == 0) {
        Out += '}';
        return;
      }
      Out += c;
    }
    fail("unterminated replacement field");
  }

  // Parses {name, plural, kw1 {...} kw2 {...} ...} and renders the variant
  // selected by the locale's plural rule (or all of them, when validating).
  // On entry pat[i] == ','.
  void plural(std::string_view pat, std::size_t& i, std::size_t k) {
    auto skip_spaces = [&] {
      while (i < pat.size() && pat[i] == ' ')
        ++i;
    };
    ++i; // the ',' after the name
    skip_spaces();
    if (pat.substr(i, 6) != "plural")
      fail("only 'plural' selection is supported");
    i += 6;
    skip_spaces();
    if (i == pat.size() || pat[i] != ',')
      fail("expected ',' after 'plural'");
    ++i;

    std::vector<std::pair<std::string_view, std::string_view>> variants;
    while (true) {
      while (i < pat.size() && pat[i] == ' ')
        ++i;
      if (i == pat.size())
        fail("unterminated plural");
      if (pat[i] == '}')
        break;
      std::size_t kw_start = i;
      while (i < pat.size() && pat[i] != ' ' && pat[i] != '{')
        ++i;
      std::string_view kw = pat.substr(kw_start, i - kw_start);
      while (i < pat.size() && pat[i] == ' ')
        ++i;
      if (i == pat.size() || pat[i] != '{')
        fail("expected '{' after plural category '" + std::string(kw) + "'");
      std::size_t text_start = ++i;
      for (unsigned depth = 1; depth != 0; ++i) {
        if (i == pat.size())
          fail("unterminated plural variant");
        if (pat[i] == '{')
          ++depth;
        else if (pat[i] == '}')
          --depth;
      }
      variants.emplace_back(kw, pat.substr(text_start, i - text_start - 1));
    }
    if (variants.empty())
      fail("plural with no variants");

    if (!GetInt) {
      // Validation: every variant must render.
      for (auto [kw, text] : variants)
        chunk(text, Msg.first_arg[k]);
      return;
    }

    std::string_view cat = Plural(GetInt(Msg.first_arg[k]));
    auto chosen = variants.back().second; // last variant is the fallback
    for (auto [kw, text] : variants)
      if (kw == cat) {
        chosen = text;
        break;
      }
    chunk(chosen, Msg.first_arg[k]);
  }

public:
  PatternRenderer(MessageDesc const& M, PluralRule P,
                  std::function<long(std::size_t)> G)
      : Msg(M), Plural(P), GetInt(std::move(G)) {}

  std::string render(std::string_view pattern) && {
    chunk(pattern, npos);
    return std::move(Out);
  }
};

// ---------------------------------------------------------------------------
// translate()
// ---------------------------------------------------------------------------

template <std::template_string S>
std::string translate(S const& s) {
  static_assert(names_are_identifier_like<S>(),
                "translatable placeholder names must be identifier-like; "
                "name the field with a name clause: {expr;\"name\"}");
  (void)&registered<S>; // compiling this message registers it

  MessageDesc desc{msgid<S>, Meta<S>::names, Meta<S>::first_arg,
                   Meta<S>::arg_count};

  // Untranslated messages fall back to the message id, which goes through
  // the same renderer -- so plural entries in the source locale's own
  // catalog work too.
  std::string_view pattern = desc.id;
  Locale const* loc = nullptr;
  if (auto it = locales().find(current_locale()); it != locales().end()) {
    loc = &it->second;
    if (auto m = loc->messages.find(desc.id); m != loc->messages.end())
      pattern = m->second;
  }
  PluralRule rule = loc ? loc->plural : Locale{}.plural;

  auto const& [...parts] = s.exprs();
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
      PatternRenderer(desc, rule, get_int).render(pattern);
  return std::vformat(indexed, std::make_format_args(parts...));
}

// ---------------------------------------------------------------------------
// Catalog validation: check every translation of every compiled message
// against that message's placeholder names, before anything is formatted.
// (The analogue of `msgfmt --check`, but driven by the program's own static
// metadata rather than by parsing printf directives.)
// ---------------------------------------------------------------------------

inline std::vector<std::string> validate_catalogs() {
  std::vector<std::string> errors;
  for (auto const& [loc_name, loc] : locales()) {
    for (auto const& [id, pattern] : loc.messages) {
      for (MessageDesc const& msg : all_messages()) {
        if (msg.id != id)
          continue;
        try {
          PatternRenderer(msg, loc.plural, nullptr).render(pattern);
        } catch (std::exception const& e) {
          errors.push_back("[" + std::string(loc_name) + "] " + e.what());
        }
        break;
      }
    }
  }
  return errors;
}

} // namespace i18n

#endif // I18N_TRANSLATE_H
