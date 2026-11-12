#include <meta>
#include <vector>
#include <memory>


template <class... Args>
__macro vec(Args&&... args) {
    using T = std::remove_cvref_t<Args...[0]>;

    auto emplace_back_loop = std::meta::token_sequence();
    for (std::meta::info expr : {args...}) {
        emplace_back_loop += ^^{
            vec.__emplace_back_assume_capacity(\(expr));
        };
    }

    return ^^{
        do {
            auto vec = ::std::vector<\(^^T)>();
            vec.reserve(\(sizeof...(Args)));
            \(emplace_back_loop);
            do_return vec;
        }
    };
}

auto main() -> int {

}
