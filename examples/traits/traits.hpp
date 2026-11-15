// Copyright 2026 Jump Trading LLC
//
// traits.hpp -- a prototype of Rust-trait / Swift-protocol style
// customization for C++, built on reflection and token-sequence macros.
//
//   trait!(Range, {
//       typename iterator;                       // associated types
//       typename sentinel;
//       typename value_type = std::iter_value_t<iterator>;
//       requires std::sentinel_for<sentinel, iterator>;
//
//       auto begin() -> iterator;                // required
//       auto end() -> sentinel;
//       auto empty() -> bool {                   // provided (overridable)
//           return self.begin() == self.end();
//       }
//   });
//
// The functions are written as if they were members of the conforming type,
// Self. Self is a cv-qualified (never reference) object type, and a
// function's own qualifiers are *added* to it, as they would be to an
// implicit object parameter. So Range<V> and Range<V const> are different
// conformances, each with its own associated types -- both normally provided
// by the one const-overloaded set of members (or impl functions) that C++
// types already have.
//
// A type conforms structurally (it has the members), or explicitly, by
// specializing traits::impl<Trait, T> -- which then is the *only* place
// looked (named conformance: members of the same name are irrelevant).
// Provided functions are used when neither has the function.
//
// From one declaration, the trait gives you:
//   traits::models<T, Range>      the concept (T may be a reference)
//   traits::check<Range, T>()     the same, failing with the reason why not
//   Range::begin(r)               a customization point object per function
//   Range::iterator<R>            an alias template per associated type
//   traits::as<Range>(r)          r viewed through the trait (static dispatch)
//   Range::dyn                    a type-erased reference to anything that
//                                 conforms, for traits without associated
//                                 types (dynamic dispatch)
//
// Calls through any of these are coerced to the declared signature: a
// function declared to return bool returns bool.
//
// Reading the expansion
// ---------------------
// trait!(Range, {...}) expands to an ordinary struct Range, all of it written
// out by the macro -- nothing is added later -- so `-Xclang -ast-print` shows
// the whole implementation. For each function f declared in the trait:
//
//   __custom_f(o, args...)    the type's own f: its impl's, if it has an impl
//                             (and then only there), otherwise its member
//   __dispatch_f(o, args...)  __custom_f if there is one, otherwise the
//                             default (ref<S>::__default_f)
//   f                         the customization point object: ref<S>{&r}.f(...)
//
// and then:
//
//   ref<S>    S seen through the trait: one member per trait function, with
//             the declared signature (the coercion), calling __dispatch_f on
//             S with the function's qualifiers added; the defaults, as
//             __default_f members ('self' is the ref); and the associated
//             types, as `typename [: assoc(..., ^^S, ..., i) :]`.
//   dyn       a vtable with a slot per function, filled per type with a call
//             to __dispatch_f.
//
// Reflection is used only to decide conformance (models, check) and to
// resolve the associated types; the generated code is plain C++. That is the
// job of __spec<A...> (the trait's functions, with the associated types as
// template parameters: instantiated with placeholders it gives the trait's
// shape, with the resolved types its signatures), __requires_N (the
// requirements), and __assoc_default_N (associated type defaults).
#pragma once

#include <algorithm>
#include <debugging>
#include <meta>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace traits {

using std::meta::info;
using std::meta::punctuator;
using std::meta::token_sequence;

// ---------------------------------------------------------------------------
// Explicit conformance (a "concept map" / Rust impl block): specialize for a
// cv-unqualified type. Provides static functions taking the object as their
// first parameter (overload on const to give Trait<T> and Trait<T const>
// different behavior or associated types), and optionally member typedefs
// naming associated types.
// ---------------------------------------------------------------------------
template <class Trait, class T>
struct impl;

template <class T>
using base_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <class Trait, class O>
concept has_impl = requires { sizeof(impl<Trait, base_t<O>>); };

template <class T, class Trait>
concept models = Trait::template __satisfied<std::remove_reference_t<T>>;

// as<Range>(r) -- view r through the trait: the static analog of dyn.
template <class Trait, class T>
    requires models<T, Trait>
constexpr auto as(T& t) -> typename Trait::template ref<T> {
    return {&t};
}

template <int I>
struct _assoc;  // placeholder for the I-th associated type

namespace detail {
template <class Trait, class T>
consteval auto check() -> bool;
}
using detail::check;

namespace detail {

// ------------------------------------------------------------- parsing ----

// A function parameter, as written -- but always named (p0, p1, ... if it
// wasn't), so that it can be passed on.
struct param {
    token_sequence decl;  // 'std::string& out', 'int p0'
    token_sequence name;  // 'out', 'p0'
    bool pack = false;    // 'Args... args'
};

struct item {
    enum kind_t { assoc, req, fn } kind;
    token_sequence name;       // assoc: its name; fn: the function name ('f', 'operator()')
    std::string_view logical;  // its name for generated entities ("call" for operator())
    bool is_call = false;      // fn: is operator()
    token_sequence decl;       // fn: declaration without body; req: the expression
    token_sequence body;       // fn: the body (with braces), if any
    token_sequence dflt;       // assoc: default, if any

    // fn: the declaration's parts, 'auto name(params) quals -> ret'
    std::vector<param> params;
    bool is_const = false, is_volatile = false, is_rvalue = false;  // the quals
    token_sequence ret;
};

consteval auto is_opener(std::meta::token t) -> bool {
    return t == punctuator::left_paren || t == punctuator::left_square
        || t == punctuator::left_brace || t == punctuator::less;
}

consteval auto is_closer(std::meta::token t) -> bool {
    return t == punctuator::right_paren || t == punctuator::right_square
        || t == punctuator::right_brace || t == punctuator::greater;
}

// The position of the first p outside any brackets, counting '<' '>' as
// brackets (in a parameter declaration, they are), or npos.
consteval auto find_top_level(token_sequence ts, punctuator p) -> size_t {
    int depth = 0;
    for (size_t i = 0; i < ts.size(); ++i) {
        if (depth == 0 && ts[i] == p) {
            return i;
        }
        if (is_opener(ts[i])) {
            ++depth;
        }
        else if (is_closer(ts[i])) {
            --depth;
        }
        else if (ts[i] == punctuator::greater_greater) {
            depth -= 2;
        }
    }
    return ts.npos;
}

consteval auto split_top_level(token_sequence ts, punctuator p) -> std::vector<token_sequence> {
    std::vector<token_sequence> parts;
    for (size_t at = find_top_level(ts, p); at != ts.npos; at = find_top_level(ts, p)) {
        parts.push_back(ts.subseq(0, at));
        ts = ts.subseq(at + 1);
    }
    parts.push_back(ts);
    return parts;
}

// Whether a parameter declaration (default argument removed) names its
// parameter: it ends in an identifier that follows something that could be
// its type ('int x', 'T const x', 'std::string& s' -- not 'T', 'const T',
// 'std::string', 'typename T::type').
consteval auto is_named(token_sequence d) -> bool {
    size_t n = d.size();
    if (n < 2 || !is_identifier(d[n - 1]) || d[n - 2] == punctuator::colon_colon) {
        return false;
    }
    for (auto kw : {^^{ struct }, ^^{ class }, ^^{ enum }, ^^{ union }, ^^{ typename }}) {
        if (d[n - 2] == kw) {
            return false;
        }
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        if (is_identifier(d[i]) || is_closer(d[i]) || d[i] == punctuator::star
            || d[i] == punctuator::ampersand || d[i] == punctuator::ampersand_ampersand
            || d[i] == punctuator::ellipsis) {
            return true;
        }
        for (auto kw : {^^{ int },
                 ^^{ char },
                 ^^{ bool },
                 ^^{ short },
                 ^^{ long },
                 ^^{ float },
                 ^^{ double },
                 ^^{ signed },
                 ^^{ unsigned },
                 ^^{ auto },
                 ^^{ wchar_t },
                 ^^{ char8_t },
                 ^^{ char16_t },
                 ^^{ char32_t }}) {
            if (d[i] == kw) {
                return true;
            }
        }
    }
    return false;
}

consteval auto parse_params(token_sequence ts) -> std::vector<param> {
    std::vector<param> params;
    if (ts.empty() || (ts.size() == 1 && ts[0] == ^^{ void })) {
        return params;
    }
    for (int k = 0; token_sequence t : split_top_level(ts, punctuator::comma)) {
        param p;
        token_sequence d = split_top_level(t, punctuator::equals)[0];
        p.pack = find_top_level(d, punctuator::ellipsis) != d.npos;
        if (is_named(d)) {
            p.name = d.subseq(d.size() - 1);
            p.decl = d;
        }
        else {
            p.name = token_sequence{std::meta::id("p", k)};
            p.decl = d + p.name;
        }
        ++k;
        params.push_back(p);
    }
    return params;
}

consteval auto parse(token_sequence ts) -> std::vector<item> {
    // strip the enclosing braces
    if (!ts.empty() && ts[0] == punctuator::left_brace) {
        ts = ts.subseq(1, ts.size() - 2);
    }

    std::vector<item> items;
    size_t i = 0;
    while (i < ts.size()) {
        size_t semi = ts.balanced_find(punctuator::semicolon, i);
        size_t lbrace0 = ts.balanced_find(punctuator::left_brace, i);
        if (semi == ts.npos && lbrace0 == ts.npos) {
            std::consteval_error_str("trait", "missing ';' in trait body");
        }

        if (ts[i] == ^^{ typename }) {
            auto toks = ts.subseq(i + 1, semi - i - 1);
            item it{.kind = item::assoc};
            it.name = toks.subseq(0, 1);
            it.logical = identifier_of(toks[0]);
            if (toks.size() > 1) {
                if (toks[1] != punctuator::equals) {
                    std::consteval_error_str("trait", "expected 'typename name [= default];'");
                }
                it.dflt = toks.subseq(2);
            }
            items.push_back(it);
            i = semi + 1;
        }
        else if (ts[i] == ^^{ requires }) {
            items.push_back({.kind = item::req, .decl = ts.subseq(i + 1, semi - i - 1)});
            i = semi + 1;
        }
        else {
            size_t lbrace = ts.balanced_find(punctuator::left_brace, i);
            size_t end = semi;
            item it{.kind = item::fn};
            if (lbrace < semi) {
                size_t rbrace = ts.balanced_find(punctuator::right_brace, lbrace + 1);
                it.decl = ts.subseq(i, lbrace - i);
                it.body = ts.subseq(lbrace, rbrace - lbrace + 1);
                end = rbrace;
                if (end + 1 < ts.size() && ts[end + 1] == punctuator::semicolon) {
                    ++end;
                }
            }
            else {
                it.decl = ts.subseq(i, semi - i);
            }

            auto const& d = it.decl;
            size_t lp = d.balanced_find(punctuator::left_paren);
            if (lp != d.npos && lp > 0 && d[lp - 1] == ^^{ operator }&&lp + 1 < d.size()
                && d[lp + 1] == punctuator::right_paren) {
                it.is_call = true;
                it.logical = "call";
                it.name = d.subseq(lp - 1, 3);
                if (!it.body.empty()) {
                    std::consteval_error_str("trait", "a default operator() is not supported");
                }
            }
            else {
                if (lp == d.npos || lp == 0 || !is_identifier(d[lp - 1])) {
                    std::consteval_error_str("trait", "expected a function declaration");
                }
                it.name = d.subseq(lp - 1, 1);
                it.logical = identifier_of(d[lp - 1]);
            }

            // auto name(params) quals -> ret
            size_t open = it.is_call ? lp + 2 : lp;
            size_t close = d.balanced_find(punctuator::right_paren, open + 1);
            size_t arrow = d.balanced_find(punctuator::arrow, close + 1);
            if (close == d.npos || arrow == d.npos) {
                std::consteval_error_str(
                    "trait", "a trait function is declared 'auto name(params) -> return-type'");
            }
            it.params = parse_params(d.subseq(open + 1, close - open - 1));
            token_sequence quals = d.subseq(close + 1, arrow - close - 1);
            it.is_const = quals.find(^^{ const }) != quals.npos;
            it.is_volatile = quals.find(^^{ volatile }) != quals.npos;
            it.is_rvalue = quals.find(punctuator::ampersand_ampersand) != quals.npos;
            it.ret = d.subseq(arrow + 1);
            items.push_back(it);
            i = end + 1;
        }
    }
    return items;
}

// ----------------------------------------------------------- reflection ---

// The member of a generated trait struct named by the pieces of an identifier:
// member(trait, "__result_", "begin") is Trait::__result_begin.
template <class... Ts>
consteval auto member(info cls, Ts const&... parts) -> std::optional<info> {
    return lookup(
        cls, identifier_of(std::meta::id(parts...)), std::meta::access_context::unchecked());
}

// The functions declared in a trait (in an instantiation of its __spec).
consteval auto functions_of(info spec) -> std::vector<info> {
    return interface_of(spec, std::meta::access_context::unchecked());
}

// The object expression's type for a member declared in the trait with the
// given qualifiers, invoked on Self: the qualifiers are *added* to Self, just
// as the implicit object parameter of a member function would be.
consteval auto object_type(info self, info m) -> info {
    info t = self;
    if (is_const(m)) {
        t = add_const(t);
    }
    if (is_volatile(m)) {
        t = add_volatile(t);
    }
    return is_rvalue_reference_qualified(m) ? add_rvalue_reference(t) : add_lvalue_reference(t);
}

// The name used for a trait function's generated entities: its identifier,
// or "call" for operator().
consteval auto logical_name(info m) -> std::string_view {
    if (is_operator_function(m)) {
        if (operator_of(m) != std::meta::operators::op_parentheses) {
            std::consteval_error_str("trait", "the only operator supported is operator()");
        }
        return "call";
    }
    return identifier_of(m);
}

// The types a trait function is called with on Self: the object (Self, with
// the function's qualifiers added), then its parameters.
consteval auto call_types(info self, info m) -> std::vector<info> {
    std::vector<info> types = {object_type(self, m)};
    for (info p : parameters_of(m)) {
        types.push_back(type_of(p));
    }
    return types;
}

// Is `name(args...)` dispatchable? (can_substitute on the __result_ alias is
// not SFINAE-friendly, so ask a requires-expression first.)
consteval auto viable(info trait, std::string_view name, std::span<info const> args) -> bool {
    return extract<bool>(substitute(*member(trait, "__viable_", name), args));
}

// What the generated code tells the library about a trait: the names of its
// associated types, and the spelling of its requirements.
struct desc {
    std::vector<std::string_view> assoc;
    std::vector<std::string_view> reqs;
};

struct resolution {
    std::vector<info> assoc;  // the associated types for Self
    info spec{};              // __spec<assoc...>
    std::string error;        // empty if satisfied
};

// The pieces of a diagnostic, run together.
template <class... Ts>
consteval auto cat(Ts const&... parts) -> std::string {
    std::string s;
    ((s += std::string_view(parts)), ...);
    return s;
}

consteval auto resolve(info trait, info self, desc const& d) -> resolution {
    auto const& names = d.assoc;
    resolution res;
    auto fail = [&](std::string msg) {
        res.error = cat(display_string_of(trait), "<", display_string_of(self), ">: ", msg);
        return res;
    };

    size_t const N = names.size();
    std::vector<info> placeholders, assoc(N);
    for (size_t i = 0; i < N; ++i) {
        placeholders.push_back(substitute(^^_assoc, {std::meta::reflect_constant(int(i))}));
    }
    // Which associated type a placeholder is (N if t isn't one).
    auto placeholder_index = [&](info t) -> size_t {
        return std::ranges::find(placeholders, dealias(t)) - placeholders.begin();
    };

    info spec_tmpl = *member(trait, "__spec");
    info impl_t = substitute(^^impl, {trait, remove_cv(self)});

    // 1. Associated types named explicitly by an impl.
    if (is_complete_type(impl_t)) {
        for (size_t i = 0; i < N; ++i) {
            auto m = lookup(impl_t, names[i], std::meta::access_context::unchecked());
            if (m && is_type(*m)) {
                assoc[i] = dealias(*m);
            }
        }
    }

    // 2. Deduced from the return types of the required functions.
    info shape = substitute(spec_tmpl, placeholders);
    for (info m : functions_of(shape)) {
        size_t idx = placeholder_index(return_type_of(m));
        if (idx == N || assoc[idx] != info{}) {
            continue;
        }

        auto name = logical_name(m);
        for (info p : parameters_of(m)) {
            if (placeholder_index(remove_cvref(type_of(p))) != N) {
                return fail(cat("cannot deduce '",
                    names[idx],
                    "' from '",
                    name,
                    "' since its parameters use associated types"));
            }
        }
        auto args = call_types(self, m);
        if (!viable(trait, name, args)) {
            return fail(cat("no viable '", name, "' (needed to deduce '", names[idx], "')"));
        }
        assoc[idx] = dealias(substitute(*member(trait, "__result_", name), args));
    }

    // 3. Defaults, in declaration order.
    for (size_t i = 0; i < N; ++i) {
        if (assoc[i] != info{}) {
            continue;
        }
        auto dflt = member(trait, "__assoc_default_", names[i]);
        std::vector<info> prev(assoc.begin(), assoc.begin() + i);
        if (!dflt || !can_substitute(*dflt, prev)) {
            return fail(cat("cannot determine associated type '", names[i], "'"));
        }
        assoc[i] = dealias(substitute(*dflt, prev));
    }

    // 4. Requirements on the associated types.
    for (size_t k = 0; k < d.reqs.size(); ++k) {
        if (!extract<bool>(substitute(*member(trait, "__requires_", k), assoc))) {
            std::string msg = cat("requirement '", d.reqs[k], "' not satisfied with");
            for (size_t i = 0; i < N; ++i) {
                msg += cat(" ", names[i], "=", display_string_of(assoc[i]));
            }
            return fail(msg);
        }
    }

    // 5. Every function, with its real signature, must be callable on Self and
    //    its result convertible to the declared return type.
    res.spec = substitute(spec_tmpl, assoc);
    for (info m : functions_of(res.spec)) {
        auto name = logical_name(m);
        auto args = call_types(self, m);
        if (!viable(trait, name, args)) {
            if (extract<bool>(*member(trait, "__has_default_", name))) {
                continue;  // the default body will be used
            }
            return fail(cat("no viable '", name, "' for ", display_string_of(args[0])));
        }
        info r = dealias(substitute(*member(trait, "__result_", name), args));
        info ret = return_type_of(m);
        if (!is_void_type(ret) && !is_convertible_type(r, ret)) {
            return fail(cat("'",
                name,
                "' returns ",
                display_string_of(r),
                ", which is not convertible to ",
                display_string_of(ret)));
        }
    }

    res.assoc = assoc;
    return res;
}

consteval auto satisfied(info trait, info self, desc const& d) -> bool {
    return resolve(trait, self, d).error.empty();
}

// The I-th associated type of Self's conformance: what the generated
// 'ref<S>::iterator' and the like are.
consteval auto assoc(info trait, info self, desc const& d, size_t i) -> info {
    resolution r = resolve(trait, self, d);
    if (!r.error.empty()) {
        std::consteval_error_str("trait-not-satisfied", r.error);
    }
    return r.assoc[i];
}

// static_assert(traits::check<Range, Foo>()) -- as models<Foo, Range>, but
// with the reason for failure as the diagnostic.
template <class Trait, class T>
consteval auto check() -> bool {
    auto r = resolve(^^Trait, ^^T, Trait::__desc());
    if (!r.error.empty()) {
        std::consteval_error_str("trait-not-satisfied", r.error);
    }
    return true;
}

// ------------------------------------------------------- the trait macro --

consteval auto spec_head_of(token_sequence assoc_params) -> token_sequence {
    return assoc_params.empty() ? ^^{ class... } : assoc_params;
}

// trait!(Name, {...}) or trait!(Name<class T, ...>, {...})
consteval auto generate(token_sequence name, token_sequence body) -> token_sequence {
    std::vector<item> items = parse(body);

    // A trait template: Name<head>.
    token_sequence base = name.subseq(0, 1), head;
    bool is_template = name.size() > 1;
    if (is_template) {
        if (name[1] != punctuator::less || name.back() != punctuator::greater) {
            std::consteval_error_str("trait", "expected 'Name' or 'Name<template-parameters>'");
        }
        head = name.subseq(2, name.size() - 3);
    }

    std::meta::list_builder assoc_params(^^{ , }), assoc_names(^^{ , }), req_texts(^^{ , });
    token_sequence defaults, spec_members, dispatchers, cpos, aliases;
    token_sequence ref_assoc, ref_members, ref_defaults;
    token_sequence dyn_slots, dyn_inits, dyn_members;
    token_sequence reqs;
    int nreqs = 0;
    size_t nassoc = 0;
    token_sequence prev_params;  // "class a, class b" for assoc declared so far
    std::vector<std::string_view> seen_fns;

    for (item const& it : items) {
        if (it.kind == item::assoc) {
            if (!it.dflt.empty()) {
                defaults += ^^{
                    template <$prev_params>
                    using ${ std::meta::id("__assoc_default_", it.logical) } =
                        ${ it.dflt };
                };
            }
            size_t index = nassoc++;
            ref_assoc += ^^{
                using ${ it.name } = typename [: ::traits::detail::assoc(
                    dealias(^^__self), ^^S, __desc(), $index) :];
            };
            assoc_params += ^^{ class ${ it.name } };
            prev_params = assoc_params;
            assoc_names += std::meta::str_lit(it.logical);
            aliases += ^^{
                template <class R>
                using ${ it.name } = typename ref<std::remove_reference_t<R>>::${ it.name };
            };
        }
    }

    // Each requirement sees all of the associated types (it may name one
    // declared after it).
    for (item const& it : items) {
        if (it.kind != item::req) {
            continue;
        }
        reqs += ^^{
            template <${ spec_head_of(assoc_params) }>
            static constexpr bool ${ std::meta::id("__requires_", nreqs++) } = ${ it.decl };
        };
        req_texts += std::meta::str_lit(std::meta::stringize(it.decl));
    }

    // How many functions share each name (a dyn's slots need distinct names).
    auto count = [&](std::string_view logical) {
        return std::ranges::count_if(
            items, [&](item const& it) { return it.kind == item::fn && it.logical == logical; });
    };

    for (int k = 0; item const& it : items) {
        if (it.kind != item::fn) {
            continue;
        }
        spec_members += it.decl + ^^{ ; };

        // The pieces of the function, to write it out in ref<S> and dyn.
        std::meta::list_builder params(^^{ , }), args(^^{ , });
        for (param const& p : it.params) {
            params += p.decl;
            args += ^^{ static_cast<decltype(${ p.name })&&>(${ p.name }) }+(
                p.pack ? ^^{ ... } : ^^{});
        }
        // The object's type: Self, with the function's qualifiers added.
        auto object = [&](token_sequence self) {
            return self + (it.is_const ? ^^{ const } : ^^{})
                 + (it.is_volatile ? ^^{ volatile } : ^^{}) + (it.is_rvalue ? ^^{ && } : ^^{ & });
        };
        auto dispatch = std::meta::id("__dispatch_", it.logical);
        auto f = it.name;
        auto ret = it.ret;

        std::meta::list_builder ref_args(^^{ , });
        ref_args += ^^{ static_cast<${ object(^^{ S }) }>(*__p) };
        ref_args += args;
        ref_members += ^^{
            constexpr auto $f($params) const -> $ret {
                return __self::$dispatch($ref_args);
            }
        };

        if (!it.body.empty()) {
            std::meta::list_builder default_params(^^{ , });
            default_params += ^^{ this auto&& self };
            default_params += params;
            ref_defaults += ^^{
                constexpr auto ${ std::meta::id("__default_", it.logical) }($default_params)
                    -> $ret ${ it.body }
            };
        }

        // dyn: a slot per function, filled per type with a call to its dispatch.
        auto slot = count(it.logical) == 1 ? std::meta::id(it.logical)
                                           : std::meta::id(it.logical, "_", k);
        token_sequence erased = it.is_const ? ^^{ void const* } : ^^{ void* };
        token_sequence cast = it.is_const ? ^^{ T const* } : ^^{ T* };
        std::meta::list_builder slot_params(^^{ , }), slot_args(^^{ , }), vt_args(^^{ , });
        slot_params += erased + ^^{ __obj };
        slot_params += params;
        slot_args += ^^{ static_cast<${ object(^^{ T }) }>(*static_cast<$cast>(__obj)) };
        slot_args += args;
        vt_args += ^^{ __obj };
        vt_args += args;
        dyn_slots += ^^{ $ret (*$slot)($slot_params); };
        dyn_inits += ^^{
            .$slot = +[]($slot_params) -> $ret { return __self::$dispatch($slot_args); },
        };
        dyn_members += ^^{
            constexpr auto $f($params) const -> $ret { return __vt->$slot($vt_args); }
        };
        ++k;

        // The rest is per name (it serves every overload).
        if (std::ranges::contains(seen_fns, it.logical)) {
            continue;
        }
        seen_fns.push_back(it.logical);

        auto n = [&](std::string_view prefix) { return std::meta::id(prefix, it.logical); };
        token_sequence impl_fn = ^^{ ::traits::impl<__self, ::traits::base_t<O>>::$f };
        token_sequence via_impl = ^^{ $impl_fn(static_cast<O&&>(o), static_cast<A&&>(a)...) };
        token_sequence via_member = ^^{ static_cast<O&&>(o).$f(static_cast<A&&>(a)...) };
        if (it.is_call) {
            via_member = ^^{ static_cast<O&&>(o)(static_cast<A&&>(a)...) };
        }
        token_sequence via_default = ^^{
            ref<std::remove_reference_t<O>>{&o}.${ n("__default_") }(static_cast<A&&>(a)...)
        };

        // __custom_f: the type's own implementation -- its impl, if it has one
        // (exclusively), otherwise its member. __dispatch_f: that, or the default.
        dispatchers += ^^{
            template <class O, class... A>
                requires (::traits::has_impl<__self, O>
                    ? requires(O&& o, A&&... a) { $via_impl; }
                    : requires(O&& o, A&&... a) { $via_member; })
            static constexpr auto ${ n("__custom_") }(O&& o, A&&... a) -> decltype(auto) {
                if constexpr (::traits::has_impl<__self, O>) {
                    return $via_impl;
                }
                else {
                    return $via_member;
                }
            }
            template <class O, class... A>
            using ${ n("__result_") } =
                decltype(${ n("__custom_") }(std::declval<O>(), std::declval<A>()...));
            template <class O, class... A>
            static constexpr bool ${ n("__viable_") } =
                requires { ${ n("__custom_") }(std::declval<O>(), std::declval<A>()...); };
            static constexpr bool ${ n("__has_default_") } = ${ !it.body.empty() };
        };
        if (it.body.empty()) {
            dispatchers += ^^{
                template <class O, class... A>
                    requires ${ n("__viable_") }<O, A...>
                static constexpr auto ${ n("__dispatch_") }(O&& o, A&&... a) -> decltype(auto) {
                    return ${ n("__custom_") }(static_cast<O&&>(o), static_cast<A&&>(a)...);
                }
            };
        }
        else {
            dispatchers += ^^{
                template <class O, class... A>
                static constexpr auto ${ n("__dispatch_") }(O&& o, A&&... a) -> decltype(auto) {
                    if constexpr (${ n("__viable_") }<O, A...>) {
                        return ${ n("__custom_") }(static_cast<O&&>(o), static_cast<A&&>(a)...);
                    }
                    else {
                        return $via_default;
                    }
                }
            };
        }

        // Name::f(x, args...) -- a customization point object
        token_sequence callee = ^^{ ref<std::remove_reference_t<R>>{&r} };
        if (!it.is_call) {
            callee += ^^{ .$f };
        }
        cpos += ^^{
            static constexpr struct {
                template <class R, class... A>
                    requires ::traits::models<R, __self>
                constexpr auto operator()(R&& r, A&&... a) const -> decltype(auto) {
                    return $callee(static_cast<A&&>(a)...);
                }
            } ${ std::meta::id(it.logical) }{};
        };
    }

    token_sequence spec_head = spec_head_of(assoc_params);
    token_sequence template_head = is_template ? ^^{ template <$head> } : ^^{};

    // A type-erased reference to anything conforming -- for a trait without
    // associated types (which would differ from type to type).
    token_sequence dyn = ^^{
        struct dyn {
            struct __vtable {
                $dyn_slots
            };
            template <class T>
            static constexpr __vtable __vtable_for = {$dyn_inits};

            void* __obj;
            __vtable const* __vt;

            template <class T>
                requires(!std::same_as<std::remove_cvref_t<T>, dyn>) && ::traits::models<T, __self>
            constexpr dyn(T& t)
                : __obj(const_cast<void*>(static_cast<void const*>(std::addressof(t))))
                , __vt(&__vtable_for<T>) {}

            $dyn_members
        };
    };

    return ^^{
        $template_head
        struct $base {
            using __self = $base;

            static consteval auto __desc() -> ::traits::detail::desc {
                return { { $assoc_names }, { $req_texts } };
            }

            template <$spec_head>
            struct __spec { $spec_members };

            $reqs

            $defaults
            $dispatchers

            // S viewed through the trait. Its associated types are those of
            // S's conformance; its functions call the trait's dispatch, which
            // finds S's impl, its member, or the default.
            template <class S>
            struct ref {
                static_assert(::traits::check<__self, S>());
                S* __p;
                $ref_assoc
                $ref_members
                $ref_defaults
            };

            template <class S>
            static constexpr bool __satisfied =
                ::traits::detail::satisfied(dealias(^^__self), ^^S, __desc());

            $aliases

            $cpos

            ${ nassoc == 0 ? dyn : ^^{} }
        };
    };
}

}  // namespace detail
}  // namespace traits

// The last argument is the body; everything before it, the (possibly
// templated, so possibly comma-containing) name.
__macro trait(std::meta::token_sequence first, std::meta::token_sequence... rest) {
    std::vector<std::meta::token_sequence> args = {first, rest...};
    std::meta::list_builder name(^^{ , });
    for (auto const& a : std::span(args).first(args.size() - 1)) {
        name += a;
    }
    return ::traits::detail::generate(name, args.back());
}
