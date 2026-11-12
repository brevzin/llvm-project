#include <meta>

namespace std::meta {
    consteval auto current_class(info scope = access_context::current().scope()) -> info {
        while (true) {
            if (is_type(scope)) {
                return scope;
            }

            if (is_namespace(scope)) {
                throw 1;
            }

            scope = parent_of(scope);
        }
    }
}

template <class R = std::initializer_list<std::meta::info>>
consteval auto inject_structured_bindings(R&& _bindings, std::meta::info cls = std::meta::current_class()) -> void {
    auto const bindings = std::vector(std::from_range, _bindings);
    auto const size = std::ranges::size(bindings);

    queue_injection(^^std, ^^{
        template <>
        struct tuple_size<\(cls)>
            : integral_constant<size_t, \(size)>
        { };
    });

    for (size_t i = 0; i < size; ++i) {
        queue_injection(^^std, ^^{
            template <>
            struct tuple_element<\(i), \(cls)> {
                using type = \(bindings[i]);
            };
        });
    }

    queue_injection(^^{
        template <size_t I, class Self>
        constexpr auto get(this Self&& self) -> auto&& {
            static consteval auto bindings = \(bindings);
            return ((Self&&)self).[: bindings[I] :];
        }
    });
}

template <class T>
class wide_result {
    T hi;
    T lo;

public:
    constexpr wide_result(T hi, T lo) : hi(hi), lo(lo) { }

    // static constexpr std::meta::info tuple_elements[] = {^^hi, ^^lo};
    consteval {
        inject_structured_bindings({^^hi, ^^lo});
    }
};

// namespace std {
//     template <class T>
//     concept ProvidesTupleElements = requires {
//         { auto(T::tuple_elements[0]) } -> same_as<meta::info>;
//     };

//     template <ProvidesTupleElements T>
//     struct tuple_size<T> : integral_constant<size_t, size(T::tuple_elements)> { };

//     template <size_t I, ProvidesTupleElements T>
//     struct tuple_element<I, T> {
//         using type = [: []{
//             auto elem = T::tuple_elements[I];
//             return is_type(elem) ? elem : type_of(elem);
//         }() :];
//     };
// }

// template <size_t I, class T>
//     requires (is_nonstatic_data_member(std::remove_cvref_t<T>::tuple_elements[I]))
// constexpr auto get(T&& t) -> auto&& {
//     return ((T&&)t).[: std::remove_cvref_t<T>::tuple_elements[I] :];
// }

auto main() -> int {
    static_assert(std::same_as<std::tuple_element_t<0, wide_result<int>>, int>);
    static_assert(std::same_as<std::tuple_element_t<1, wide_result<int>>, int>);
    auto [hi, lo] = wide_result<int>(1, 2);
}
