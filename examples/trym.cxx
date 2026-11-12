#include <expected>
#include <array>
#include <memory>
#include <meta>
#include <iostream>
#include <debugging>

template <class T>
__macro fwd(T&& t) {
    return ^^{ static_cast<\(type_of(t))&&>(\(t)) };
}

template <class T> struct try_traits;
template <class T, class E>
struct try_traits<std::expected<T, E>> {
    static constexpr auto should_continue(std::expected<T, E> const& e) -> bool {
        return e.has_value();
    }

    static constexpr auto extract_continue([[clang::lifetimebound]] auto&& e) -> auto&& {
        return *fwd!(e);
    }

    static constexpr auto extract_break([[clang::lifetimebound]] auto&& e) -> auto&& {
        return fwd!(e).error();
    }

    static constexpr auto from_break(auto&& err) {
        return std::expected<T, E>(std::unexpect, fwd!(err));
    }
};

template <class T>
__macro try_(T&& e) {
    namespace m = std::meta;

    // `try_` only means anything inside a function: it early-returns from one.
    m::info where = m::macro_expansion_context();
    if (!m::is_function(where))
        std::consteval_error_str("bad-try-context",
                                 "try_ must be invoked inside a function");

    // Computed in the macro body -- NOT injected as `using` aliases.
    m::info CT = m::substitute(^^try_traits, {m::remove_cvref(m::type_of(e))});
    m::info RT = m::substitute(^^try_traits, {m::return_type_of(where)});

    return ^^{
        do [__r=\(e)] -> decltype(auto) {
            if (not \(CT)::should_continue(__r)) [[unlikely]] {
                return \(RT)::from_break(\(CT)::extract_break(fwd!(__r)));
            }
            do_return \(CT)::extract_continue(fwd!(__r));
        }
    };
}

enum class E { };
template <class T>
auto get_data() -> std::expected<T, E>;

auto f1() -> std::expected<int, E> {
    auto&& data = try_!(get_data<int>()); // <== warning about dangling reference on this line
    return data;
}

auto consume(int x) -> int { return x; }

auto f2() -> std::expected<int, E> {
    auto&& data = consume(try_!(get_data<int>())); // <== no warning here
    return data;
}
