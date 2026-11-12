#include <experimental/meta>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <vector>

namespace iface {

// Named probe templates for the adaptor path: the base iterator type is
// complete, so these are ordinary requires-expressions substituted via
// reflection.
template <class B> constexpr bool base_can_deref   = requires(B const& b) { *b; };
template <class B> constexpr bool base_can_pre_inc = requires(B& b) { ++b; };
template <class B> constexpr bool base_can_eq      =
    requires(B const& a, B const& b) { { a == b } -> std::convertible_to<bool>; };

struct iter_config {
    std::meta::info iterator_concept;
    std::meta::info value_type;
    std::meta::info difference_type = ^^std::ptrdiff_t;
};

// Which operators has the user declared SO FAR in the class being defined?
// Declaration-existence via members_of: never forms a call, so
// incompleteness is irrelevant. (Protocol rule: primitives precede the
// consteval block.)
// CAVEAT (measured): members_of does NOT enumerate friend declarations —
// friends are not members. So a user opting out of a derived operation by
// declaring their own must use a MEMBER operator (or the library needs a
// separate "friends declared so far" query, which doesn't exist yet). A
// hidden friend the walk can't see collides with an injected friend at
// overload resolution: ambiguity at the use site.
consteval bool declares_op(std::meta::info cls, std::meta::operators which) {
    for (auto m : members_of(cls, std::meta::access_context::unchecked()))
        if (is_operator_function(m) && operator_of(m) == which)
            return true;
    return false;
}

consteval bool declares_fn(std::meta::info cls, std::string_view name) {
    for (auto m : members_of(cls, std::meta::access_context::unchecked()))
        if (has_identifier(m) && identifier_of(m) == name)
            return true;
    return false;
}

consteval void iterator_interface(std::meta::info cls, iter_config cfg) {
    using namespace std::meta;
    auto ts = ^^{
        using iterator_concept = \(cfg.iterator_concept);
        using value_type = \(cfg.value_type);
        using difference_type = \(cfg.difference_type);
    };

    bool adaptor  = declares_fn(cls, "base_reference");
    bool plus_eq  = declares_op(cls, op_plus_equals);
    bool minus    = declares_op(cls, op_minus);
    bool pre_inc  = declares_op(cls, op_plus_plus);
    bool pre_dec  = declares_op(cls, op_minus_minus);
    bool eq       = declares_op(cls, op_equals_equals);
    bool deref    = declares_op(cls, op_star);

    if (adaptor) {
        // Adaptor path: the base iterator's type is COMPLETE, so full
        // semantic probing via requires-expressions is available. Forward
        // whatever the base supports and the user didn't override.
        info base_t = remove_reference(return_type_of(
            (members_of(cls, access_context::unchecked())
             | std::views::filter([](info m) {
                   return has_identifier(m) && identifier_of(m) == "base_reference";
               })).front()));
        bool base_deref   = extract<bool>(substitute(^^base_can_deref, {base_t}));
        bool base_pre_inc = extract<bool>(substitute(^^base_can_pre_inc, {base_t}));
        bool base_eq      = extract<bool>(substitute(^^base_can_eq, {base_t}));

        if (base_deref && !deref)
            ts += ^^{ constexpr decltype(auto) operator*() const { return *base_reference(); } };
        if (base_pre_inc && !pre_inc)
            ts += ^^{ constexpr auto operator++() -> \(cls)& { ++base_reference(); return *this; } };
        if (base_pre_inc)
            ts += ^^{ constexpr auto operator++(int) -> \(cls) { auto tmp = *this; ++*this; return tmp; } };
        if (base_eq && !eq)
            ts += ^^{
                friend constexpr bool operator==(\(cls) const& a, \(cls) const& b) {
                    return a.base_reference() == b.base_reference();
                }
            };
        queue_injection(ts);
        return;
    }

    // Direct path: plain functions, chosen by declaration existence. Wrong
    // signatures error in the injected bodies at definition time — the
    // protocol's contract, not a constraint to dodge.
    if (plus_eq) {
        ts += ^^{
            constexpr auto operator++() -> \(cls)& { *this += 1; return *this; }
            constexpr auto operator++(int) -> \(cls) { auto tmp = *this; ++*this; return tmp; }
            constexpr auto operator--() -> \(cls)& { *this += -1; return *this; }
            constexpr auto operator--(int) -> \(cls) { auto tmp = *this; --*this; return tmp; }
            constexpr auto operator-=(std::ptrdiff_t n) -> \(cls)& { *this += -n; return *this; }
            constexpr decltype(auto) operator[](std::ptrdiff_t n) const { auto tmp = *this; tmp += n; return *tmp; }
            friend constexpr auto operator+(\(cls) it, std::ptrdiff_t n) -> \(cls) { it += n; return it; }
            friend constexpr auto operator+(std::ptrdiff_t n, \(cls) it) -> \(cls) { it += n; return it; }
            friend constexpr auto operator-(\(cls) it, std::ptrdiff_t n) -> \(cls) { it += -n; return it; }
        };
    } else {
        if (pre_inc)
            ts += ^^{ constexpr auto operator++(int) -> \(cls) { auto tmp = *this; ++*this; return tmp; } };
        if (pre_dec)
            ts += ^^{ constexpr auto operator--(int) -> \(cls) { auto tmp = *this; --*this; return tmp; } };
    }
    if (minus) {
        if (!eq)
            ts += ^^{
                friend constexpr bool operator==(\(cls) const& a, \(cls) const& b) { return (a - b) == 0; }
            };
        ts += ^^{
            friend constexpr auto operator<=>(\(cls) const& a, \(cls) const& b) { return (a - b) <=> 0; }
        };
    }
    queue_injection(ts);
}

} // namespace iface

// ==== Direct random-access user =============================================
struct repeated_chars_iterator {
    constexpr repeated_chars_iterator() : first_(nullptr), size_(0), n_(0) {}
    constexpr repeated_chars_iterator(char const* f, std::ptrdiff_t s, std::ptrdiff_t n)
        : first_(f), size_(s), n_(n) {}
    constexpr char operator*() const { return first_[n_ % size_]; }
    constexpr repeated_chars_iterator& operator+=(std::ptrdiff_t i) { n_ += i; return *this; }
    constexpr std::ptrdiff_t operator-(repeated_chars_iterator other) const { return n_ - other.n_; }

    consteval {
        iface::iterator_interface(^^repeated_chars_iterator,
            {.iterator_concept = ^^std::random_access_iterator_tag, .value_type = ^^char});
    }

private:
    char const* first_;
    std::ptrdiff_t size_;
    std::ptrdiff_t n_;
};
static_assert(std::random_access_iterator<repeated_chars_iterator>);

constexpr bool test_ra() {
    repeated_chars_iterator it("ab", 2, 0);
    auto j = it + 3;
    return *j == 'b' && j - it == 3 && it < j && it[2] == 'a' && it != j;
}
static_assert(test_ra());

// ==== Adaptor user: filtered_int_iterator style ==============================
// Provides base_reference() + its own ++ (to do the filtering); everything
// else forwards to the (complete!) base iterator type.
struct skip_odd_iterator {
    using base_t = int const*;

    constexpr skip_odd_iterator() : it_(nullptr) {}
    constexpr explicit skip_odd_iterator(int const* it) : it_(it) {}

    constexpr base_t& base_reference() { return it_; }
    constexpr base_t const& base_reference() const { return it_; }

    // User's own ++: skip to next even value.
    constexpr skip_odd_iterator& operator++() {
        do { ++it_; } while (*it_ % 2 != 0);
        return *this;
    }

    consteval {
        iface::iterator_interface(^^skip_odd_iterator,
            {.iterator_concept = ^^std::forward_iterator_tag, .value_type = ^^int});
    }

private:
    int const* it_;
};
static_assert(std::forward_iterator<skip_odd_iterator>);

constexpr bool test_adapt() {
    constexpr int data[] = {2, 3, 5, 4, 7, 6, 0};
    skip_odd_iterator a(data);           // 2
    auto b = a;
    ++b;                                  // user's ++: skips 3, 5 -> 4
    if (*b != 4) return false;            // injected *: forwards to base
    auto c = b++;                         // injected postfix
    if (*c != 4 || *b != 6) return false;
    return a != b && c != b && a == a;    // injected ==: base comparison
}
static_assert(test_adapt());
int main() {}
// Appended: verify overriding — a user == in the adaptor beats forwarding.
struct override_iter {
    using base_t = int const*;
    constexpr override_iter() : it_(nullptr) {}
    constexpr base_t& base_reference() { return it_; }
    constexpr base_t const& base_reference() const { return it_; }
    constexpr int operator*() const { return *it_; }  // user's own *
    constexpr override_iter& operator++() { ++it_; return *this; }
    constexpr bool operator==(override_iter const&) const { return true; } // always-equal (member: visible to members_of)

    consteval {
        iface::iterator_interface(^^override_iter,
            {.iterator_concept = ^^std::forward_iterator_tag, .value_type = ^^int});
    }
private:
    int const* it_;
};
constexpr int arr2[] = {1,2};
static_assert(override_iter{} == override_iter{});  // user's ==, not base forwarding
