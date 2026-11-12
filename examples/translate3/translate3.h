// translate3.h - a self-contained internationalization library on C++26
// template strings (P3951), demonstrating the *external* translation
// workflow that real i18n pipelines use:
//
//   extract                    translate                 load + format
//   -------                    ---------                 -------------
//   messages are compiled  =>  .pot template goes   =>   .po catalogs are
//   into an ELF section        to translators, who       loaded at runtime;
//   and dumped by objcopy,     return .po files per      lookup by message
//   without running the        locale (with a real       id, plural chosen
//   program                    Plural-Forms formula)     by the catalog's
//                                                        own formula
//
// Like examples/translate2, translate() is lazy: it returns a rebound
// pattern (std::rebound_format), so the result flows into std::format /
// format_to / print / println with no intermediate formatted string.
//
// Message ids are synthesized at compile time with *named* placeholders:
// t"Hello, {name}!" has id "Hello, {name}!". The name is the interpolated
// expression, or the field's name clause: {u.display_name();"user"}.
// Translations may reorder, repeat, or drop placeholders and may use
// plural selection:
//
//   msgid "Removed {n} files."
//   msgstr "{n, plural, 0 {Usunieto # plik.} 1 {# pliki.} 2 {# plikow.}}"
//
// where the variant keys are the indices computed by the catalog header's
// gettext-style Plural-Forms expression, and '#' formats the selector.

#ifndef I18N_TRANSLATE3_H
#define I18N_TRANSLATE3_H

#include <array>
#include <cstddef>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace i18n {

// ===========================================================================
// Compile-time message metadata
// ===========================================================================

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
      n += 1 + cstrlen_(S::interpolation(i).expression); // {name
      n += cstrlen_(S::interpolation(i).fmt) - 1;        // :spec} or }
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

template <std::template_string S>
inline constexpr auto msgid_storage = build_msgid<S>();

template <std::template_string S>
inline constexpr std::string_view msgid{msgid_storage<S>.data(),
                                        msgid_storage<S>.size() - 1};

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

// ===========================================================================
// Build-time extraction: every translate()d message deposits a .pot entry
// into a dedicated ELF section, recoverable from the compiled artifact with
//   objcopy -O binary --only-section=i18n_messages app messages.bin
//   tr -s '\0' '\n' < messages.bin > messages.pot
// No execution required; sees through macros; includes messages in code
// that never runs; cannot drift from the binary actually shipped.
// ===========================================================================

template <class T>
consteval std::string_view type_name() {
  std::string_view p = __PRETTY_FUNCTION__;
  auto start = p.find("T = ") + 4;
  return p.substr(start, p.rfind(']') - start);
}

// Writes one record; Sink is either a size counter or a buffer writer, so
// the same code sizes and fills the constexpr array.
template <std::template_string S, class... Args>
consteval void write_pot_record(auto sink) {
  auto text = [&](std::string_view s) {
    for (char c : s)
      sink(c);
  };
  auto quoted = [&](std::string_view s) {
    for (char c : s) {
      if (c == '"' || c == '\\')
        sink('\\');
      if (c == '\n') {
        sink('\\');
        sink('n');
        continue;
      }
      sink(c);
    }
  };

  if constexpr (Meta<S>::N > 0) {
    text("#. placeholders:");
    std::string_view types[]{type_name<Args>()...};
    for (std::size_t i = 0; i < Meta<S>::N; ++i) {
      text(i ? ", " : " ");
      text(Meta<S>::names[i]);
      text(": ");
      text(types[Meta<S>::first_arg[i]]);
    }
    sink('\n');
  }
  text("msgid \"");
  quoted(msgid<S>);
  text("\"\nmsgstr \"\"\n");
}

template <std::template_string S, class... Args>
consteval std::size_t pot_record_size() {
  std::size_t n = 0;
  write_pot_record<S, Args...>([&](char) { ++n; });
  return n;
}

template <std::template_string S, class... Args>
consteval auto build_pot_record() {
  std::array<char, pot_record_size<S, Args...>() + 1> buf{};
  std::size_t o = 0;
  write_pot_record<S, Args...>([&](char c) { buf[o++] = c; });
  return buf;
}

template <std::template_string S, class... Args>
[[gnu::used, gnu::section("i18n_messages")]]
inline constexpr auto pot_entry = build_pot_record<S, Args...>();

// ===========================================================================
// Runtime message registry (for catalog validation)
// ===========================================================================

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

// ===========================================================================
// gettext Plural-Forms expressions: "n==1 ? 0 : n%10>=2 && ... ? 1 : 2".
// Evaluated per lookup by a tiny recursive-descent interpreter; the result
// index selects among a translation's plural variants (keyed "0", "1", ...).
// ===========================================================================

class PluralForms {
  std::string Expr = "n != 1"; // the C/English default

  struct Eval {
    std::string_view s;
    std::size_t i = 0;
    long n;

    void ws() {
      while (i < s.size() && s[i] == ' ')
        ++i;
    }
    bool lit(std::string_view tok) {
      ws();
      if (s.substr(i, tok.size()) != tok)
        return false;
      // Don't let '<' match the front of '<=', or '=' the front of '=='.
      if ((tok == "<" || tok == ">") && i + 1 < s.size() && s[i + 1] == '=')
        return false;
      i += tok.size();
      return true;
    }
    [[noreturn]] void fail() {
      throw std::runtime_error("bad Plural-Forms expression: " +
                               std::string(s));
    }

    long ternary() {
      long c = logic_or();
      if (lit("?")) {
        long t = ternary();
        if (!lit(":"))
          fail();
        long f = ternary();
        return c ? t : f;
      }
      return c;
    }
    long logic_or() {
      long v = logic_and();
      while (lit("||"))
        v = logic_and() || v;
      return v;
    }
    long logic_and() {
      long v = equality();
      while (lit("&&")) {
        long r = equality();
        v = v && r;
      }
      return v;
    }
    long equality() {
      long v = relational();
      while (true) {
        if (lit("=="))
          v = (v == relational());
        else if (lit("!="))
          v = (v != relational());
        else
          return v;
      }
    }
    long relational() {
      long v = modulo();
      while (true) {
        if (lit("<="))
          v = (v <= modulo());
        else if (lit(">="))
          v = (v >= modulo());
        else if (lit("<"))
          v = (v < modulo());
        else if (lit(">"))
          v = (v > modulo());
        else
          return v;
      }
    }
    long modulo() {
      long v = primary();
      while (lit("%")) {
        long r = primary();
        v = r ? v % r : 0;
      }
      return v;
    }
    long primary() {
      if (lit("!"))
        return !primary();
      if (lit("(")) {
        long v = ternary();
        if (!lit(")"))
          fail();
        return v;
      }
      ws();
      if (i < s.size() && s[i] == 'n') {
        ++i;
        return n;
      }
      if (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        long v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
          v = v * 10 + (s[i++] - '0');
        return v;
      }
      fail();
    }
  };

public:
  PluralForms() = default;
  explicit PluralForms(std::string expr) : Expr(std::move(expr)) {}

  // Returns the plural index as the variant key it selects.
  std::string_view category(long n) const {
    static constexpr std::string_view keys[] = {"0", "1", "2", "3", "4",
                                                "5", "6", "7", "8", "9"};
    Eval e{Expr, 0, n};
    long idx = e.ternary();
    return keys[idx >= 0 && idx <= 9 ? idx : 0];
  }
};

// ===========================================================================
// Locales, catalogs, and the PO loader
// ===========================================================================

struct Locale {
  std::map<std::string, std::string, std::less<>> messages;
  PluralForms plural;
};

inline std::map<std::string, Locale, std::less<>>& locales() {
  static std::map<std::string, Locale, std::less<>> m;
  return m;
}

inline std::string& current_locale() {
  static std::string loc = "en";
  return loc;
}

inline void set_locale(std::string_view loc) {
  current_locale().assign(loc);
}

inline std::string po_unescape(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      switch (s[++i]) {
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      default: out += s[i]; break;
      }
    } else {
      out += s[i];
    }
  }
  return out;
}

// Loads a PO-syntax catalog: msgid/msgstr entries with continuation lines,
// '#' comments, and a header (msgid "") whose Plural-Forms field supplies
// the plural expression. (Demo dialect: plural variants live inside the
// msgstr pattern, keyed by the expression's indices, rather than in
// msgstr[n] entries; msgctxt is not supported.)
inline bool load_po(std::string_view locale_name, std::string const& path) {
  std::ifstream in(path);
  if (!in)
    return false;

  Locale loc;
  std::string id, str;
  enum { None, Id, Str } state = None;

  auto commit = [&] {
    if (state != Str)
      return;
    if (id.empty()) {
      // The header. Fish out "Plural-Forms: nplurals=N; plural=EXPR;".
      if (auto p = str.find("plural="); p != std::string::npos) {
        auto end = str.find(';', p);
        loc.plural = PluralForms(str.substr(p + 7, end - (p + 7)));
      }
    } else {
      loc.messages[id] = str;
    }
    id.clear();
    str.clear();
    state = None;
  };
  auto quoted = [&](std::string_view line) {
    auto first = line.find('"');
    auto last = line.rfind('"');
    return first == std::string_view::npos || last <= first
               ? std::string()
               : po_unescape(line.substr(first + 1, last - first - 1));
  };

  for (std::string line; std::getline(in, line);) {
    std::string_view l = line;
    while (!l.empty() && (l.back() == '\r' || l.back() == ' '))
      l.remove_suffix(1);
    if (l.starts_with("msgid ")) {
      commit();
      state = Id;
      id += quoted(l);
    } else if (l.starts_with("msgstr ")) {
      state = Str;
      str += quoted(l);
    } else if (l.starts_with("\"")) {
      (state == Id ? id : str) += quoted(l);
    }
    // '#' comments and blank lines: nothing to do.
  }
  commit();

  locales()[std::string(locale_name)] = std::move(loc);
  return true;
}

// ===========================================================================
// Pattern renderer: rewrites a translated pattern with named placeholders
// into a std::format pattern with numeric arg-ids (which is what makes
// reordering work), selecting plural variants along the way.
// ===========================================================================

class PatternRenderer {
  static constexpr std::size_t npos = std::size_t(-1);

  MessageDesc const& Msg;
  PluralForms const& Plural;
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

  // Renders one chunk of pattern text. plural_arg is the argument whose
  // value '#' formats, when inside a plural variant.
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

  // Parses {name, plural, key1 {...} key2 {...} ...} and renders the variant
  // whose key is the plural index computed by the locale's Plural-Forms
  // expression (or all variants, when validating). On entry pat[i] == ','.
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
      skip_spaces();
      if (i == pat.size())
        fail("unterminated plural");
      if (pat[i] == '}')
        break;
      std::size_t kw_start = i;
      while (i < pat.size() && pat[i] != ' ' && pat[i] != '{')
        ++i;
      std::string_view kw = pat.substr(kw_start, i - kw_start);
      skip_spaces();
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

    std::string_view cat = Plural.category(GetInt(Msg.first_arg[k]));
    auto chosen = variants.back().second; // last variant is the fallback
    for (auto [kw, text] : variants)
      if (kw == cat) {
        chosen = text;
        break;
      }
    chunk(chosen, Msg.first_arg[k]);
  }

public:
  PatternRenderer(MessageDesc const& M, PluralForms const& P,
                  std::function<long(std::size_t)> G)
      : Msg(M), Plural(P), GetInt(std::move(G)) {}

  std::string render(std::string_view pattern) && {
    chunk(pattern, npos);
    return std::move(Out);
  }
};

// ===========================================================================
// translate(): lazy -- returns a std::rebound_format, so the result
// flows into std::format / format_to / print / println unformatted. Pattern
// lookup, plural selection and renumbering happen here (they need the
// values, which are already bound); formatting stays deferred.
// ===========================================================================

template <std::template_string S>
auto translate(S ts) {
  static_assert(names_are_identifier_like<S>(),
                "translatable placeholder names must be identifier-like; "
                "name the field with a name clause: {expr;\"name\"}");
  (void)&registered<S>; // compiling this message registers it

  MessageDesc desc{msgid<S>, Meta<S>::names, Meta<S>::first_arg,
                   Meta<S>::arg_count};

  auto const& [...parts] = ts.exprs();
  (void)&pot_entry<S, std::remove_cvref_t<decltype(parts)>...>; // extraction

  static const PluralForms default_plural;
  std::string_view pattern = desc.id;
  PluralForms const* plural = &default_plural;
  if (auto it = locales().find(current_locale()); it != locales().end()) {
    plural = &it->second.plural;
    if (auto m = it->second.messages.find(desc.id);
        m != it->second.messages.end())
      pattern = m->second;
  }

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
      PatternRenderer(desc, *plural, get_int).render(pattern);

  // std::rebind_format pairs the owned, renumbered pattern with the
  // original template string's arguments, so formatting stays deferred.
  return std::rebind_format(std::move(indexed), std::move(ts));
}

// ===========================================================================
// Catalog validation: check every translation of every compiled message
// against that message's placeholder names, before anything is formatted.
// (msgfmt --check, but driven by the program's own static metadata.)
// ===========================================================================

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
          errors.push_back("[" + loc_name + "] " + e.what());
        }
        break;
      }
    }
  }
  return errors;
}

} // namespace i18n

#endif // I18N_TRANSLATE3_H
