#ifndef _LIBCPP___FORMAT_TEMPLATE_STRING_H
#define _LIBCPP___FORMAT_TEMPLATE_STRING_H

#include <__concepts/convertible_to.h>
#include <__format/format_functions.h>
#include <__format/template_string_fwd.h>
#include <string_view>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_HAS_TEMPLATE_STRINGS

// rebind_format: the adaptor every template string transformer ends with --
// pair a runtime pattern with the arguments of an existing template string.
// The pattern is stored exactly as passed: a std::string transfers
// ownership, a string_view or pointer references storage the caller keeps
// alive. The result is necessarily runtime-checked: only a consteval fmt()
// (i.e. a literal's) can satisfy format_string's compile-time checking, so
// a runtime pattern travels as a dynamic format string.
// Rebinding a bound pattern replaces it (declared here, befriended below):
// the invariant that a rebound_format pairs a pattern directly with a
// template string always holds, and types do not nest.
template <class _Pattern, class _P0, template_string _Sp>
  requires convertible_to<const _Pattern&, string_view>
constexpr auto rebind_format(_Pattern __pattern,
                             rebound_format<_P0, _Sp> __s);

template <class _Pattern, template_string _Sp>
class rebound_format {
  _Pattern __pattern_;
  _Sp __s_;

  template <class _Pattern2, class _P0, template_string _Sp2>
    requires convertible_to<const _Pattern2&, string_view>
  friend constexpr auto rebind_format(_Pattern2, rebound_format<_P0, _Sp2>);

public:
  _LIBCPP_HIDE_FROM_ABI constexpr rebound_format(_Pattern __pattern, _Sp __s)
      : __pattern_(std::move(__pattern)), __s_(std::move(__s)) {}

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr string_view pattern() const {
    return string_view(__pattern_);
  }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI auto fmt() const {
    return std::dynamic_format(pattern());
  }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI decltype(auto) exprs() const {
    return __s_.exprs();
  }
};

template <template_string _Sp, class _Pattern>
  requires convertible_to<const _Pattern&, string_view>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto
rebind_format(_Pattern __pattern, _Sp __s) {
  return rebound_format<_Pattern, _Sp>(std::move(__pattern), std::move(__s));
}

template <class _Pattern, class _P0, template_string _Sp>
  requires convertible_to<const _Pattern&, string_view>
constexpr auto rebind_format(_Pattern __pattern,
                             rebound_format<_P0, _Sp> __s) {
  return rebound_format<_Pattern, _Sp>(std::move(__pattern),
                                       std::move(__s.__s_));
}

template <__deferred_format _S>
[[nodiscard]] _LIBCPP_ALWAYS_INLINE _LIBCPP_HIDE_FROM_ABI string
format(_S&& __s) {
    auto& [...__parts] = __s.exprs();
    return std::format(__s.fmt(), __parts...);
}

template <output_iterator<const char&> _OutIt, __deferred_format _S>
_LIBCPP_ALWAYS_INLINE _LIBCPP_HIDE_FROM_ABI _OutIt
format_to(_OutIt __out_it, _S&& __s) {
    auto& [...__parts] = __s.exprs();
    return std::format_to(std::move(__out_it), __s.fmt(), __parts...);
}

// A literal operator template taking a parameter is only valid with the
// language feature enabled.
inline namespace literals {
inline namespace string_literals {

template <__deferred_format _S>
inline _LIBCPP_HIDE_FROM_ABI string
operator""s(_S&& __s) {
    auto& [...__parts] = __s.exprs();
    return std::format(__s.fmt(), __parts...);
}

}
}


#endif // _LIBCPP_HAS_TEMPLATE_STRINGS

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS


#endif
