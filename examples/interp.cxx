#include <fmt/format.h>
#include <fmt/color.h>
#include <ranges>
#include <string>
#include <print>
#include <meta>

// This stuff is a workaround for clang not yet having
// constexpr structured bindings and some bugs with
// pack indexing.
template <size_t... Vs> struct seq {
    template <class T, class F>
    static constexpr auto unpack(T&& obj, F f) {
        auto& [...pieces] = obj;
        f(pieces...[Vs]...);
    }

    template <class F>
    static constexpr auto with_indices(F f) {
        return f(std::integral_constant<size_t, Vs>()...);
    }
};

template <size_t From, size_t N>
using make_seq = [: []{
    std::vector<std::meta::info> vals;
    for (size_t I = 0; I < N; ++I) {
        vals.push_back(std::meta::reflect_constant(From + I));
    }
    return substitute(^^seq, vals);
}() :];

// the implementation of std::print looks like this:
//  ```cpp
//  template <std::template_string S>
//  auto print(S&& s) -> void {
//      auto& [...exprs] = s;
//      std::print(s.fmt(), exprs...);
//  }
//  ```
// (not included here because it's in the stdlib)

template <std::template_string S>
auto highlight_print(S&& s, fmt::text_style style) -> void {
    fmt::print(s.string(0));
    auto& [...exprs] = s;

    template for (constexpr int I : std::views::iota(0zu, s.num_interpolations())) {
        static constexpr auto interp = s.interpolation(I);

        // In standard C++26, this could just be
        //  ```cpp
        //  constexpr auto [...J] = std::make_index_sequence<interp.count>();
        //  fmt::print(style, interp.fmt, exprs...[interp.index + J]...);
        //  ```
        // but clang doesn't implement constexpr structured bindings yet and
        // appears to have some other bugs with pack indexing and expansion
        // statements, so this was my best workaround
        #ifdef FAILS
        constexpr auto [...J] = std::make_index_sequence<interp.count>();
        fmt::print(style, interp.fmt, exprs...[interp.index + J]...);
        #else
        make_seq<interp.index, interp.count>::unpack(s, [&](auto&... pieces){
            fmt::print(style,
                       interp.fmt,
                       pieces...);
        });
        #endif

        fmt::print(s.string(I + 1));
    }
}

template <std::template_string S>
auto show_json(S&& s) -> void {
    // instead of using Boost.Json or something, we'll just build up a string and print it
    std::string result = "{";

    char const* delim = "";
    auto& [...exprs] = s;
    template for (constexpr int I : std::views::iota(0zu, s.num_interpolations())) {
        static constexpr auto interp = s.interpolation(I);
        std::format_to(std::back_inserter(result), t"{delim}{interp.expression:?}:");

        auto const& cur = exprs...[interp.index];

        // It'd be cool if we had like a real debug output mechanism, but...
        if constexpr (std::convertible_to<decltype(cur), std::string_view>) {
            std::format_to(std::back_inserter(result), t"{cur:?}");
        } else {
            std::format_to(std::back_inserter(result), t"{cur}");
        }

        delim = ",";
    }

    result.push_back('}');
    std::println("JSON: {}", result);
}

template <std::template_string S>
auto with_printf(S&& s) -> void {
    constexpr char const* fmt_string = [&]() consteval {
        std::string fmt = s.string(0);
        auto nsdms = nonstatic_data_members_of(remove_cvref(^^S), std::meta::access_context::current());

        for (size_t i = 0; i < s.num_interpolations(); ++i) {
            // in theory, this logic would be more interesting
            auto interp = s.interpolation(i);
            auto type = remove_cvref(type_of(nsdms[interp.index]));
            if (type == ^^int) {
                fmt += "%d";
            } else {
                if (interp.fmt == std::string_view("{:?}")) {
                    fmt += "\"%s\"";
                } else {
                    fmt += "%s";
                }
            }

            fmt += s.string(i + 1);
        }

        return std::define_static_string(fmt);
    }();

    auto adjust = []<class T>(T const& arg){
        if constexpr (std::same_as<T, std::string>) {
            return arg.c_str();
        } else {
            return arg;
        }
    };

    auto& [...exprs] = s;
    #ifdef FAILS
    constexpr auto [...Is] = std::make_index_sequence<s.num_interpolations()>();
    std::printf(fmt_string, adjust(exprs...[s.interpolation(Is).index])...);
    #else
    make_seq<0, s.num_interpolations()>::with_indices([&](auto... Is){
        std::printf(fmt_string, adjust(exprs...[s.interpolation(Is).index])...);
    });
    #endif
}

auto verb() -> std::string { return "die"; }

auto main() -> int {
    std::string name = "Inigo Montoya";
    std::string relation = "father";
    int width = 5;

    auto s = t"Hello, my name is {name:?}. You killed my {relation}. Prepare to {verb():*^{width}}.\n";
    std::print(s);
    highlight_print(s, fmt::emphasis::bold | fg(fmt::color::green));
    show_json(s);
    with_printf(s);
}
